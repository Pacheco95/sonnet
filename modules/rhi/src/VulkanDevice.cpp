#include "VulkanDevice.h"

#include "VulkanSwapchain.h"
#include "VulkanTypes.h"

#include <sonnet/core/Assert.h>
#include <sonnet/core/Error.h>
#include <sonnet/core/Log.h>
#include <sonnet/core/Profile.h>

#include <VkBootstrap.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <limits>
#include <string>
#include <utility>

namespace sonnet::rhi {

namespace {

// Per-frame host-visible memory for uniform and storage data. Sized for the editor's scenes
// until the renderer measures a real need; exhaustion is logged, never fatal.
constexpr std::uint64_t TransientBufferSize = 8u << 20;

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                             VkDebugUtilsMessageTypeFlagsEXT type,
                                             const VkDebugUtilsMessengerCallbackDataEXT *data, void *userData) {
  auto *device = static_cast<VulkanDevice *>(userData);
  device->onDebugMessage(static_cast<vk::DebugUtilsMessageSeverityFlagBitsEXT>(severity),
                         static_cast<vk::DebugUtilsMessageTypeFlagsEXT>(type),
                         *reinterpret_cast<const vk::DebugUtilsMessengerCallbackDataEXT *>(data));
  return VK_FALSE;
}

std::string versionString(std::uint32_t version) {
  return std::format("{}.{}.{}", VK_API_VERSION_MAJOR(version), VK_API_VERSION_MINOR(version),
                     VK_API_VERSION_PATCH(version));
}

template <typename T> [[nodiscard]] T unwrap(vkb::Result<T> result, std::string_view what) {
  if (!result) {
    throw core::Exception{std::format("{}: {}", what, result.error().message()), core::ErrorCategory::Graphics};
  }
  return std::move(result.value());
}

template <typename T> std::uint64_t objectHandle(T object) {
  return reinterpret_cast<std::uint64_t>(static_cast<typename T::NativeType>(object));
}

} // namespace

VulkanDevice::VulkanDevice(const DeviceDesc &desc)
    : m_context(desc.platform->vulkanGetInstanceProcAddr()), m_commandList(*this) {
  SONNET_ASSERT(desc.platform != nullptr, "a device needs the platform for the Vulkan loader and surfaces");
  createInstance(desc);
  selectAndCreateDevice(desc);
  createAllocator();
  createPipelineLayout();
  createFrames();
  SONNET_LOG_INFO("Vulkan {} device \"{}\", driver {} {}, loader {}{}", versionString(m_info.apiVersion),
                  m_info.deviceName, m_info.driverName, m_info.driverInfo, versionString(m_info.loaderVersion),
                  m_info.validationEnabled ? ", validation on" : "");
}

VulkanDevice::~VulkanDevice() {
  waitIdle();
  for (Frame &frame : m_frames) {
    for (auto &destroy : frame.garbage) {
      destroy();
    }
    frame.garbage.clear();
    if (frame.transientBuffer) {
      m_buffers.remove(frame.transientBuffer);
    }
  }
  reportLeaks();
  m_pipelines.clear();
  m_shaders.clear();
  m_buffers.clear();
  m_images.clear();
}

void VulkanDevice::createInstance(const DeviceDesc &desc) {
  const PFN_vkGetInstanceProcAddr loader = desc.platform->vulkanGetInstanceProcAddr();
  const vkb::SystemInfo systemInfo = unwrap(vkb::SystemInfo::get_system_info(loader), "querying Vulkan system info");

  bool validation = desc.enableValidation;
  if (validation && !systemInfo.validation_layers_available) {
    SONNET_LOG_WARN("validation requested but VK_LAYER_KHRONOS_validation is not installed");
    validation = false;
  }
  const bool debugUtils = systemInfo.is_extension_available(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

  vkb::InstanceBuilder builder{loader};
  // 1.4 is required of the device, not of the loader: distributions ship older loaders (Ubuntu
  // 24.04 has 1.3) in front of 1.4 drivers, and the engine uses no 1.4 instance-level entry
  // points. 1.1 gives vkEnumerateInstanceVersion and the properties2 queries the selector needs.
  builder.set_app_name(desc.applicationName.c_str())
      .set_engine_name("Sonnet")
      .require_api_version(1, 4, 0)
      .set_minimum_instance_version(1, 1, 0)
      .enable_validation_layers(validation);
  for (const char *extension : desc.platform->vulkanInstanceExtensions()) {
    builder.enable_extension(extension);
  }
  if (debugUtils) {
    builder.enable_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)
        .set_debug_callback(debugCallback)
        .set_debug_callback_user_data_pointer(this)
        .set_debug_messenger_severity(
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        .set_debug_messenger_type(VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT);
  }
  if (validation) {
    builder.add_validation_feature_enable(VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT);
  }

  // vk-bootstrap creates; the RAII wrappers own from here on and vkb::destroy_* is never called.
  const vkb::Instance instance = unwrap(builder.build(), "creating the Vulkan instance");
  m_instance = vk::raii::Instance{m_context, instance.instance};
  if (instance.debug_messenger != VK_NULL_HANDLE) {
    m_messenger = vk::raii::DebugUtilsMessengerEXT{m_instance, instance.debug_messenger};
  }
  m_info.validationEnabled = validation;
  m_info.apiVersion = instance.api_version;
  m_info.loaderVersion = instance.instance_version;
  SONNET_LOG_DEBUG("Vulkan loader {}", versionString(m_info.loaderVersion));
}

void VulkanDevice::selectAndCreateDevice(const DeviceDesc &) {
  // The features in docs/rendering.md, "Vulkan baseline". Extended dynamic state is core in 1.3
  // without a feature bit.
  VkPhysicalDeviceVulkan11Features features11{};
  features11.shaderDrawParameters = VK_TRUE; // Slang lowers SV_VertexID through gl_BaseVertex

  VkPhysicalDeviceVulkan12Features features12{};
  features12.timelineSemaphore = VK_TRUE;
  features12.bufferDeviceAddress = VK_TRUE;
  features12.scalarBlockLayout = VK_TRUE;
  features12.descriptorIndexing = VK_TRUE;
  features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
  features12.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;
  features12.descriptorBindingPartiallyBound = VK_TRUE;
  features12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
  features12.descriptorBindingStorageImageUpdateAfterBind = VK_TRUE;
  features12.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
  features12.descriptorBindingUpdateUnusedWhilePending = VK_TRUE;
  features12.runtimeDescriptorArray = VK_TRUE;

  VkPhysicalDeviceVulkan13Features features13{};
  features13.dynamicRendering = VK_TRUE;
  features13.synchronization2 = VK_TRUE;
  // Slang lowers a fragment shader's discard to OpDemoteToHelperInvocation.
  features13.shaderDemoteToHelperInvocation = VK_TRUE;

  VkPhysicalDeviceVulkan14Features features14{};
  features14.pushDescriptor = VK_TRUE;
  features14.dynamicRenderingLocalRead = VK_TRUE;
  features14.maintenance5 = VK_TRUE;
  features14.maintenance6 = VK_TRUE;

  vkb::Instance instanceRef{};
  instanceRef.instance = *m_instance;
  instanceRef.fp_vkGetInstanceProcAddr = m_context.getDispatcher()->vkGetInstanceProcAddr;
  instanceRef.instance_version = m_info.apiVersion;
  instanceRef.api_version = m_info.apiVersion;

  vkb::PhysicalDeviceSelector selector{instanceRef};
  // Surfaces come later, from windows; selection only needs the swapchain extension.
  vkb::PhysicalDevice physicalDevice = unwrap(selector.set_minimum_version(1, 4)
                                                  .defer_surface_initialization()
                                                  .set_required_features_11(features11)
                                                  .set_required_features_12(features12)
                                                  .set_required_features_13(features13)
                                                  .set_required_features_14(features14)
                                                  .select(),
                                              "selecting a Vulkan 1.4 device");
  physicalDevice.enable_extension_if_present(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);

  const vkb::Device device = unwrap(vkb::DeviceBuilder{physicalDevice}.build(), "creating the Vulkan device");
  m_physicalDevice = vk::raii::PhysicalDevice{m_instance, physicalDevice.physical_device};
  m_device = vk::raii::Device{m_physicalDevice, device.device};
  m_graphicsFamily = unwrap(device.get_queue_index(vkb::QueueType::graphics), "finding the graphics queue");
  m_graphicsQueue = vk::raii::Queue{m_device, m_graphicsFamily, 0};
  m_info.deviceName = physicalDevice.name;
  m_info.apiVersion = physicalDevice.properties.apiVersion;
  const auto properties =
      m_physicalDevice.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceDriverProperties>();
  const auto &driver = properties.get<vk::PhysicalDeviceDriverProperties>();
  m_info.driverName = driver.driverName.data();
  m_info.driverInfo = driver.driverInfo.data();
  const vk::PhysicalDeviceLimits &limits = properties.get<vk::PhysicalDeviceProperties2>().properties.limits;
  m_transientAlignment =
      std::max<std::uint64_t>({limits.minUniformBufferOffsetAlignment, limits.minStorageBufferOffsetAlignment, 16});
  m_timestampPeriod = limits.timestampPeriod;
  const std::vector<vk::QueueFamilyProperties> families = m_physicalDevice.getQueueFamilyProperties();
  m_info.timestampsSupported = families[m_graphicsFamily].timestampValidBits > 0 && m_timestampPeriod > 0.0f;
  setDebugName(vk::ObjectType::eDevice, objectHandle(*m_device), "sonnet device");
  setDebugName(vk::ObjectType::eQueue, objectHandle(*m_graphicsQueue), "graphics queue");
}

void VulkanDevice::createAllocator() {
  const vma::VulkanFunctions functions =
      vma::functionsFromDispatchers(*m_device.getDispatcher(), *m_instance.getDispatcher());
  vma::AllocatorCreateInfo info{};
  info.flags = vma::AllocatorCreateFlagBits::eBufferDeviceAddress | vma::AllocatorCreateFlagBits::eExtMemoryBudget;
  info.physicalDevice = *m_physicalDevice;
  info.device = *m_device;
  info.instance = *m_instance;
  info.vulkanApiVersion = VK_API_VERSION_1_4;
  info.pVulkanFunctions = &functions;
  m_allocator = vma::createAllocatorUnique(info);
}

void VulkanDevice::createPipelineLayout() {
  // Set 0 is the bindless set, empty until textures arrive in M3; set 1 takes the per-pass
  // buffers and one sampled image as push descriptors (docs/rendering.md, "Frame structure").
  m_bindlessLayout = vk::raii::DescriptorSetLayout{m_device, vk::DescriptorSetLayoutCreateInfo{}};
  const std::array passBindings{
      vk::DescriptorSetLayoutBinding{PassUniformBinding, vk::DescriptorType::eUniformBuffer, 1,
                                     vk::ShaderStageFlagBits::eAllGraphics},
      vk::DescriptorSetLayoutBinding{PassStorageBinding, vk::DescriptorType::eStorageBuffer, 1,
                                     vk::ShaderStageFlagBits::eAllGraphics},
      vk::DescriptorSetLayoutBinding{PassImageBinding, vk::DescriptorType::eSampledImage, 1,
                                     vk::ShaderStageFlagBits::eFragment},
  };
  m_passLayout = vk::raii::DescriptorSetLayout{
      m_device,
      vk::DescriptorSetLayoutCreateInfo{vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptor, passBindings}};
  const std::array layouts{*m_bindlessLayout, *m_passLayout};
  const vk::PushConstantRange range{vk::ShaderStageFlagBits::eAllGraphics, 0, PushConstantSize};
  m_pipelineLayout = vk::raii::PipelineLayout{m_device, vk::PipelineLayoutCreateInfo{{}, layouts, range}};
  setDebugName(vk::ObjectType::eDescriptorSetLayout, objectHandle(*m_bindlessLayout), "bindless set layout");
  setDebugName(vk::ObjectType::eDescriptorSetLayout, objectHandle(*m_passLayout), "pass set layout");
  setDebugName(vk::ObjectType::ePipelineLayout, objectHandle(*m_pipelineLayout), "shared pipeline layout");
}

void VulkanDevice::createFrames() {
  vk::SemaphoreTypeCreateInfo timelineType{vk::SemaphoreType::eTimeline, 0};
  m_timeline = vk::raii::Semaphore{m_device, vk::SemaphoreCreateInfo{{}, &timelineType}};
  setDebugName(vk::ObjectType::eSemaphore, objectHandle(*m_timeline), "frame timeline");

  for (std::uint32_t i = 0; i < FramesInFlight; ++i) {
    Frame &frame = m_frames[i];
    frame.commandPool = vk::raii::CommandPool{m_device, vk::CommandPoolCreateInfo{{}, m_graphicsFamily}};
    vk::raii::CommandBuffers buffers{
        m_device, vk::CommandBufferAllocateInfo{*frame.commandPool, vk::CommandBufferLevel::ePrimary, 1}};
    frame.commandBuffer = std::move(buffers.front());
    frame.imageAvailable = vk::raii::Semaphore{m_device, vk::SemaphoreCreateInfo{}};
    setDebugName(vk::ObjectType::eCommandPool, objectHandle(*frame.commandPool),
                 std::format("frame {} command pool", i));
    setDebugName(vk::ObjectType::eSemaphore, objectHandle(*frame.imageAvailable),
                 std::format("frame {} image available", i));
    if (m_info.timestampsSupported) {
      frame.queryPool =
          vk::raii::QueryPool{m_device, vk::QueryPoolCreateInfo{{}, vk::QueryType::eTimestamp, MaxTimestamps}};
      setDebugName(vk::ObjectType::eQueryPool, objectHandle(*frame.queryPool), std::format("frame {} timestamps", i));
    }
    frame.transientBuffer = createBuffer({.size = TransientBufferSize,
                                          .usage = BufferUsage::Uniform | BufferUsage::Storage,
                                          .memory = MemoryUsage::CpuToGpu,
                                          .debugName = std::format("frame {} transient", i)});
  }
}

std::unique_ptr<ISwapchain> VulkanDevice::createSwapchain(platform::IWindow &window) {
  return std::make_unique<VulkanSwapchain>(*this, window);
}

BufferHandle VulkanDevice::createBuffer(const BufferDesc &desc) {
  SONNET_ASSERT(desc.size > 0, "buffer \"{}\" has no size", desc.debugName);
  vk::BufferCreateInfo bufferInfo{{}, desc.size, toVk(desc.usage), vk::SharingMode::eExclusive};
  vma::AllocationCreateInfo allocationInfo{};
  allocationInfo.usage = vma::MemoryUsage::eAuto;
  switch (desc.memory) {
  case MemoryUsage::GpuOnly:
    break;
  case MemoryUsage::CpuToGpu:
    allocationInfo.flags =
        vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;
    break;
  case MemoryUsage::GpuToCpu:
    allocationInfo.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessRandom;
    break;
  }
  vma::AllocationInfo result{};
  auto [allocation, buffer] = m_allocator->createBufferUnique(bufferInfo, allocationInfo, result);
  setDebugName(vk::ObjectType::eBuffer, objectHandle(*buffer), desc.debugName);
  std::uint64_t address = 0;
  if (has(desc.usage, BufferUsage::Storage)) {
    address = m_device.getBufferAddress(vk::BufferDeviceAddressInfo{*buffer});
  }
  const BufferHandle handle = m_buffers.emplace(VulkanBuffer{desc, std::move(allocation), std::move(buffer),
                                                             static_cast<std::byte *>(result.pMappedData), address});
  SONNET_LOG_TRACE("buffer \"{}\" {} bytes -> {}:{}", desc.debugName, desc.size, handle.index, handle.generation);
  return handle;
}

void VulkanDevice::destroyBuffer(BufferHandle handle) {
  std::optional<VulkanBuffer> buffer = m_buffers.remove(handle);
  if (!buffer) {
    SONNET_LOG_WARN("destroyBuffer: stale handle {}:{}", handle.index, handle.generation);
    return;
  }
  deferDestruction([resource = std::make_shared<VulkanBuffer>(std::move(*buffer))]() mutable { resource.reset(); });
}

std::span<std::byte> VulkanDevice::mappedRange(BufferHandle handle) {
  const VulkanBuffer *buffer = m_buffers.find(handle);
  if (buffer == nullptr || buffer->mapped == nullptr) {
    return {};
  }
  return {buffer->mapped, static_cast<std::size_t>(buffer->desc.size)};
}

std::uint64_t VulkanDevice::bufferAddress(BufferHandle handle) const {
  const VulkanBuffer *buffer = m_buffers.find(handle);
  return buffer != nullptr ? buffer->address : 0;
}

ImageHandle VulkanDevice::createImage(const ImageDesc &desc) {
  SONNET_ASSERT(desc.size.x > 0 && desc.size.y > 0, "image \"{}\" has no size", desc.debugName);
  vk::ImageCreateInfo imageInfo{{},
                                vk::ImageType::e2D,
                                toVk(desc.format),
                                vk::Extent3D{desc.size.x, desc.size.y, 1},
                                1,
                                1,
                                vk::SampleCountFlagBits::e1,
                                vk::ImageTiling::eOptimal,
                                toVk(desc.usage),
                                vk::SharingMode::eExclusive};
  vma::AllocationCreateInfo allocationInfo{};
  allocationInfo.usage = vma::MemoryUsage::eAuto;
  auto [allocation, image] = m_allocator->createImageUnique(imageInfo, allocationInfo);
  setDebugName(vk::ObjectType::eImage, objectHandle(*image), desc.debugName);

  vk::ImageViewCreateInfo viewInfo{{},
                                   *image,
                                   vk::ImageViewType::e2D,
                                   toVk(desc.format),
                                   {},
                                   vk::ImageSubresourceRange{aspectOf(desc.format), 0, 1, 0, 1}};
  vk::raii::ImageView view{m_device, viewInfo};
  setDebugName(vk::ObjectType::eImageView, objectHandle(*view), std::format("{} view", desc.debugName));

  const vk::Image imageHandle = *image;
  const ImageHandle handle =
      m_images.emplace(VulkanImage{desc, std::move(allocation), std::move(image), imageHandle, std::move(view)});
  SONNET_LOG_TRACE("image \"{}\" {}x{} -> {}:{}", desc.debugName, desc.size.x, desc.size.y, handle.index,
                   handle.generation);
  return handle;
}

ImageHandle VulkanDevice::registerExternalImage(vk::Image image, const ImageDesc &desc) {
  vk::ImageViewCreateInfo viewInfo{{},
                                   image,
                                   vk::ImageViewType::e2D,
                                   toVk(desc.format),
                                   {},
                                   vk::ImageSubresourceRange{aspectOf(desc.format), 0, 1, 0, 1}};
  vk::raii::ImageView view{m_device, viewInfo};
  setDebugName(vk::ObjectType::eImage, objectHandle(image), desc.debugName);
  setDebugName(vk::ObjectType::eImageView, objectHandle(*view), std::format("{} view", desc.debugName));
  return m_images.emplace(VulkanImage{desc, {}, {}, image, std::move(view)});
}

void VulkanDevice::destroyImage(ImageHandle handle) {
  std::optional<VulkanImage> image = m_images.remove(handle);
  if (!image) {
    SONNET_LOG_WARN("destroyImage: stale handle {}:{}", handle.index, handle.generation);
    return;
  }
  deferDestruction([resource = std::make_shared<VulkanImage>(std::move(*image))]() mutable { resource.reset(); });
}

const ImageDesc &VulkanDevice::imageDesc(ImageHandle handle) const {
  return m_images.get(handle).desc;
}

ShaderHandle VulkanDevice::createShader(const ShaderDesc &desc) {
  if (desc.spirv.size() < 4 || desc.spirv.size() % 4 != 0) {
    throw core::Exception{
        std::format("shader \"{}\": {} bytes is not a SPIR-V module", desc.debugName, desc.spirv.size()),
        core::ErrorCategory::Shader};
  }
  // Copied into aligned storage; the span may come from a byte buffer.
  std::vector<std::uint32_t> words(desc.spirv.size() / 4);
  std::memcpy(words.data(), desc.spirv.data(), desc.spirv.size());
  if (words[0] != 0x07230203u) {
    throw core::Exception{std::format("shader \"{}\" has no SPIR-V magic number", desc.debugName),
                          core::ErrorCategory::Shader};
  }
  vk::raii::ShaderModule module{m_device, vk::ShaderModuleCreateInfo{{}, words}};
  setDebugName(vk::ObjectType::eShaderModule, objectHandle(*module), desc.debugName);
  return m_shaders.emplace(VulkanShader{desc.debugName, std::move(module)});
}

void VulkanDevice::destroyShader(ShaderHandle handle) {
  std::optional<VulkanShader> shader = m_shaders.remove(handle);
  if (!shader) {
    SONNET_LOG_WARN("destroyShader: stale handle {}:{}", handle.index, handle.generation);
    return;
  }
  // Modules are not referenced by submitted work; pipelines hold what they need.
  shader.reset();
}

PipelineHandle VulkanDevice::createGraphicsPipeline(const GraphicsPipelineDesc &desc) {
  const VulkanShader *shader = m_shaders.find(desc.shader);
  if (shader == nullptr) {
    throw core::Exception{std::format("pipeline \"{}\": stale shader handle", desc.debugName),
                          core::ErrorCategory::Graphics};
  }
  const std::array stages{
      vk::PipelineShaderStageCreateInfo{
          {}, vk::ShaderStageFlagBits::eVertex, *shader->module, desc.vertexEntry.c_str()},
      vk::PipelineShaderStageCreateInfo{
          {}, vk::ShaderStageFlagBits::eFragment, *shader->module, desc.fragmentEntry.c_str()},
  };
  // No vertex input: vertex data is pulled from buffers by index (docs/rendering.md).
  const vk::PipelineVertexInputStateCreateInfo vertexInput{};
  const vk::PipelineInputAssemblyStateCreateInfo inputAssembly{{}, vk::PrimitiveTopology::eTriangleList, VK_FALSE};
  const vk::PipelineViewportStateCreateInfo viewport{{}, 1, nullptr, 1, nullptr};
  vk::CullModeFlags cull = vk::CullModeFlagBits::eNone;
  switch (desc.cullMode) {
  case CullMode::None:
    break;
  case CullMode::Back:
    cull = vk::CullModeFlagBits::eBack;
    break;
  case CullMode::Front:
    cull = vk::CullModeFlagBits::eFront;
    break;
  }
  const vk::PipelineRasterizationStateCreateInfo rasterization{
      {},   VK_FALSE, VK_FALSE, vk::PolygonMode::eFill, cull, vk::FrontFace::eCounterClockwise, VK_FALSE, 0.0f,
      0.0f, 0.0f,     1.0f};
  const vk::PipelineMultisampleStateCreateInfo multisample{{}, vk::SampleCountFlagBits::e1};
  const vk::PipelineDepthStencilStateCreateInfo depthStencil{
      {}, desc.depth.test ? VK_TRUE : VK_FALSE, desc.depth.write ? VK_TRUE : VK_FALSE, toVk(desc.depth.compare)};
  std::vector<vk::PipelineColorBlendAttachmentState> blendAttachments(desc.colorFormats.size());
  for (auto &attachment : blendAttachments) {
    attachment.blendEnable = VK_FALSE;
    attachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
  }
  const vk::PipelineColorBlendStateCreateInfo colorBlend{{}, VK_FALSE, vk::LogicOp::eCopy, blendAttachments};
  const std::array dynamicStates{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
  const vk::PipelineDynamicStateCreateInfo dynamicState{{}, dynamicStates};
  std::vector<vk::Format> colorFormats;
  colorFormats.reserve(desc.colorFormats.size());
  for (const Format format : desc.colorFormats) {
    colorFormats.push_back(toVk(format));
  }
  const vk::PipelineRenderingCreateInfo rendering{0, colorFormats, toVk(desc.depthFormat)};

  const vk::GraphicsPipelineCreateInfo info{{},
                                            stages,
                                            &vertexInput,
                                            &inputAssembly,
                                            nullptr,
                                            &viewport,
                                            &rasterization,
                                            &multisample,
                                            &depthStencil,
                                            &colorBlend,
                                            &dynamicState,
                                            *m_pipelineLayout,
                                            nullptr,
                                            0,
                                            nullptr,
                                            0,
                                            &rendering};
  vk::raii::Pipeline pipeline{m_device, nullptr, info};
  setDebugName(vk::ObjectType::ePipeline, objectHandle(*pipeline), desc.debugName);
  SONNET_LOG_DEBUG("pipeline \"{}\" from shader \"{}\"", desc.debugName, shader->debugName);
  return m_pipelines.emplace(VulkanPipeline{desc.debugName, std::move(pipeline)});
}

void VulkanDevice::destroyPipeline(PipelineHandle handle) {
  std::optional<VulkanPipeline> pipeline = m_pipelines.remove(handle);
  if (!pipeline) {
    SONNET_LOG_WARN("destroyPipeline: stale handle {}:{}", handle.index, handle.generation);
    return;
  }
  deferDestruction([resource = std::make_shared<VulkanPipeline>(std::move(*pipeline))]() mutable { resource.reset(); });
}

bool VulkanDevice::isValid(ShaderHandle handle) const {
  return m_shaders.contains(handle);
}

bool VulkanDevice::isValid(PipelineHandle handle) const {
  return m_pipelines.contains(handle);
}

bool VulkanDevice::isValid(BufferHandle handle) const {
  return m_buffers.contains(handle);
}

bool VulkanDevice::isValid(ImageHandle handle) const {
  return m_images.contains(handle);
}

void VulkanDevice::deferDestruction(std::function<void()> destroy) {
  // While recording, this frame's commands may reference the resource: it goes with this slot,
  // freed when the slot is reused after the wait on this frame. Between frames the frame that was
  // just submitted may still be running, and it lives in the previous slot; the current slot's
  // next wait only covers the frame before that.
  const std::uint32_t slot = m_recording ? m_frameIndex : (m_frameIndex + FramesInFlight - 1) % FramesInFlight;
  m_frames[slot].garbage.push_back(std::move(destroy));
}

void VulkanDevice::waitForFrame(Frame &frame) {
  if (frame.submittedValue == 0) {
    return;
  }
  const vk::Semaphore semaphore = *m_timeline;
  const vk::SemaphoreWaitInfo waitInfo{{}, 1, &semaphore, &frame.submittedValue};
  const VkSemaphoreWaitInfo *raw = reinterpret_cast<const VkSemaphoreWaitInfo *>(&waitInfo);
  const VkResult result =
      m_device.getDispatcher()->vkWaitSemaphores(*m_device, raw, std::numeric_limits<std::uint64_t>::max());
  if (result != VK_SUCCESS) {
    SONNET_LOG_CRITICAL("vkWaitSemaphores failed: {}", vk::to_string(static_cast<vk::Result>(result)));
  }
}

void VulkanDevice::readTimestamps(Frame &frame) {
  frame.timestampResults.clear();
  if (frame.timestampCount == 0 || frame.submittedValue == 0) {
    return;
  }
  // The frame has completed (waitForFrame), so every written query is available. Unwritten
  // slots below the highest index come back with availability 0 and read as zero.
  std::array<std::uint64_t, MaxTimestamps * 2> raw{};
  const VkResult result = m_device.getDispatcher()->vkGetQueryPoolResults(
      *m_device, *frame.queryPool, 0, frame.timestampCount, sizeof(raw), raw.data(), 2 * sizeof(std::uint64_t),
      VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
  if (result != VK_SUCCESS && result != VK_NOT_READY) {
    SONNET_LOG_ERROR("vkGetQueryPoolResults failed: {}", vk::to_string(static_cast<vk::Result>(result)));
    return;
  }
  frame.timestampResults.resize(frame.timestampCount);
  for (std::uint32_t i = 0; i < frame.timestampCount; ++i) {
    const bool available = raw[2 * i + 1] != 0;
    frame.timestampResults[i] =
        available ? static_cast<std::uint64_t>(static_cast<double>(raw[2 * i]) * static_cast<double>(m_timestampPeriod))
                  : 0;
  }
}

ICommandList &VulkanDevice::beginFrame() {
  SONNET_ZONE();
  SONNET_ASSERT(!m_recording, "beginFrame called twice without endFrame");
  Frame &frame = m_frames[m_frameIndex];
  waitForFrame(frame);
  readTimestamps(frame);
  for (auto &destroy : frame.garbage) {
    destroy();
  }
  frame.garbage.clear();
  frame.commandPool.reset();
  frame.transientOffset = 0;
  frame.transientExhausted = false;
  frame.timestampCount = 0;
  m_commandList.begin(frame.commandBuffer);
  if (m_info.timestampsSupported) {
    // Reset in the command buffer rather than on the host so no extra device feature is needed.
    m_device.getDispatcher()->vkCmdResetQueryPool(*frame.commandBuffer, *frame.queryPool, 0, MaxTimestamps);
  }
  m_recording = true;
  return m_commandList;
}

void VulkanDevice::noteTimestamp(std::uint32_t index) noexcept {
  Frame &frame = m_frames[m_frameIndex];
  frame.timestampCount = std::max(frame.timestampCount, index + 1);
}

TransientAllocation VulkanDevice::allocateTransient(std::uint64_t size) {
  SONNET_ASSERT(m_recording, "allocateTransient outside beginFrame/endFrame");
  Frame &frame = m_frames[m_frameIndex];
  const std::uint64_t offset =
      (frame.transientOffset + m_transientAlignment - 1) / m_transientAlignment * m_transientAlignment;
  if (size == 0 || offset + size > TransientBufferSize) {
    if (!frame.transientExhausted) {
      SONNET_LOG_ERROR("transient allocator exhausted: {} bytes requested at offset {} of {}", size, offset,
                       TransientBufferSize);
      frame.transientExhausted = true;
    }
    return {};
  }
  frame.transientOffset = offset + size;
  const std::span<std::byte> mapped = mappedRange(frame.transientBuffer);
  return TransientAllocation{frame.transientBuffer, offset, mapped.subspan(offset, size)};
}

std::span<const std::uint64_t> VulkanDevice::timestamps() const {
  return m_frames[m_frameIndex].timestampResults;
}

MemoryBudget VulkanDevice::memoryBudget() const {
  MemoryBudget budget;
  const vk::PhysicalDeviceMemoryProperties properties = m_physicalDevice.getMemoryProperties();
  std::array<vma::Budget, MemoryBudget::MaxHeaps> heaps{};
  m_allocator->getHeapBudgets(heaps.data());
  budget.heapCount = std::min(properties.memoryHeapCount, MemoryBudget::MaxHeaps);
  for (std::uint32_t i = 0; i < budget.heapCount; ++i) {
    budget.heaps[i] =
        HeapBudget{heaps[i].usage, heaps[i].budget,
                   static_cast<bool>(properties.memoryHeaps[i].flags & vk::MemoryHeapFlagBits::eDeviceLocal)};
  }
  return budget;
}

void VulkanDevice::addPendingPresent(VulkanSwapchain &swapchain, std::uint32_t imageIndex,
                                     vk::Semaphore imageAvailable) {
  m_pendingPresents.push_back(PendingPresent{&swapchain, imageIndex, imageAvailable});
}

void VulkanDevice::endFrame() {
  SONNET_ZONE();
  SONNET_ASSERT(m_recording, "endFrame called without beginFrame");
  Frame &frame = m_frames[m_frameIndex];
  m_commandList.end();
  m_recording = false;

  const std::uint64_t signalValue = ++m_timelineValue;
  // Small fixed-capacity arrays: one swapchain per window, and the frame never allocates here
  // for the common single-window case.
  std::vector<vk::SemaphoreSubmitInfo> waits;
  std::vector<vk::SemaphoreSubmitInfo> signals;
  waits.reserve(m_pendingPresents.size());
  signals.reserve(m_pendingPresents.size() + 1);
  for (const PendingPresent &present : m_pendingPresents) {
    waits.emplace_back(present.imageAvailable, 0, vk::PipelineStageFlagBits2::eColorAttachmentOutput);
    // AllCommands, not ColorAttachmentOutput: the transition to Present is a barrier that
    // completes after the colour output stage, and the presentation engine has to see it.
    signals.emplace_back(present.swapchain->renderFinished(present.imageIndex), 0,
                         vk::PipelineStageFlagBits2::eAllCommands);
  }
  signals.emplace_back(*m_timeline, signalValue, vk::PipelineStageFlagBits2::eAllCommands);

  const vk::CommandBufferSubmitInfo commandInfo{*frame.commandBuffer};
  const vk::SubmitInfo2 submit{{}, waits, commandInfo, signals};
  const VkResult submitResult = m_device.getDispatcher()->vkQueueSubmit2(
      *m_graphicsQueue, 1, reinterpret_cast<const VkSubmitInfo2 *>(&submit), VK_NULL_HANDLE);
  if (submitResult != VK_SUCCESS) {
    SONNET_LOG_CRITICAL("vkQueueSubmit2 failed: {}", vk::to_string(static_cast<vk::Result>(submitResult)));
  }
  frame.submittedValue = signalValue;

  for (const PendingPresent &present : m_pendingPresents) {
    const vk::Semaphore wait = present.swapchain->renderFinished(present.imageIndex);
    const vk::SwapchainKHR swapchain = present.swapchain->handle();
    const vk::PresentInfoKHR presentInfo{1, &wait, 1, &swapchain, &present.imageIndex};
    const VkResult presentResult = m_device.getDispatcher()->vkQueuePresentKHR(
        *m_graphicsQueue, reinterpret_cast<const VkPresentInfoKHR *>(&presentInfo));
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
      present.swapchain->markOutOfDate();
    } else if (presentResult != VK_SUCCESS) {
      SONNET_LOG_ERROR("vkQueuePresentKHR failed: {}", vk::to_string(static_cast<vk::Result>(presentResult)));
    }
  }
  m_pendingPresents.clear();
  m_frameIndex = (m_frameIndex + 1) % FramesInFlight;
}

void VulkanDevice::waitIdle() {
  if (*m_device != nullptr) {
    m_device.waitIdle();
  }
}

void VulkanDevice::setDebugName(vk::ObjectType type, std::uint64_t handle, std::string_view name) const {
  if (*m_messenger == nullptr || name.empty()) {
    return;
  }
  const std::string terminated{name};
  m_device.setDebugUtilsObjectNameEXT(vk::DebugUtilsObjectNameInfoEXT{type, handle, terminated.c_str()});
}

void VulkanDevice::onDebugMessage(vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
                                  vk::DebugUtilsMessageTypeFlagsEXT type,
                                  const vk::DebugUtilsMessengerCallbackDataEXT &data) {
  std::string objects;
  for (std::uint32_t i = 0; i < data.objectCount; ++i) {
    const auto &object = data.pObjects[i];
    if (object.pObjectName != nullptr) {
      objects += objects.empty() ? " [" : ", ";
      objects += object.pObjectName;
    }
  }
  if (!objects.empty()) {
    objects += "]";
  }
  const char *id = data.pMessageIdName != nullptr ? data.pMessageIdName : "";

  // General messages are loader chatter (layer discovery, driver choice): kept at trace unless
  // something went wrong. Validation and performance messages map severity to level directly and
  // count as failures for tests.
  const bool general = !(
      type & (vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance));
  if (general) {
    if (severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eError) {
      SONNET_LOG_WARN("{}{}: {}", id, objects, data.pMessage);
    } else {
      SONNET_LOG_TRACE("{}{}: {}", id, objects, data.pMessage);
    }
    return;
  }
  switch (severity) {
  case vk::DebugUtilsMessageSeverityFlagBitsEXT::eError:
    ++m_validationMessages;
    SONNET_LOG_ERROR("{}{}: {}", id, objects, data.pMessage);
    break;
  case vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning:
    ++m_validationMessages;
    SONNET_LOG_WARN("{}{}: {}", id, objects, data.pMessage);
    break;
  case vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo:
    SONNET_LOG_DEBUG("{}{}: {}", id, objects, data.pMessage);
    break;
  case vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose:
    SONNET_LOG_TRACE("{}{}: {}", id, objects, data.pMessage);
    break;
  }
}

void VulkanDevice::reportLeaks() {
  m_buffers.forEach([](BufferHandle handle, VulkanBuffer &buffer) {
    SONNET_LOG_WARN("leaked buffer \"{}\" ({}:{})", buffer.desc.debugName, handle.index, handle.generation);
  });
  m_images.forEach([](ImageHandle handle, VulkanImage &image) {
    SONNET_LOG_WARN("leaked image \"{}\" ({}:{})", image.desc.debugName, handle.index, handle.generation);
  });
  m_shaders.forEach([](ShaderHandle handle, VulkanShader &shader) {
    SONNET_LOG_WARN("leaked shader \"{}\" ({}:{})", shader.debugName, handle.index, handle.generation);
  });
  m_pipelines.forEach([](PipelineHandle handle, VulkanPipeline &pipeline) {
    SONNET_LOG_WARN("leaked pipeline \"{}\" ({}:{})", pipeline.debugName, handle.index, handle.generation);
  });
}

} // namespace sonnet::rhi
