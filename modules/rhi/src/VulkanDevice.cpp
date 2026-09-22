#include "VulkanDevice.h"

#include "OwnerThread.h"
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
// Per-frame staging ring for uploads; larger uploads get a dedicated buffer for the frame.
constexpr std::uint64_t StagingBufferSize = 32u << 20;
// Buffer-to-image copies need offsets that are multiples of 4 and of the texel block size, which
// is at most 16 bytes for the engine's formats.
constexpr std::uint64_t StagingAlignment = 16;

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
    // Device selection says why each device was rejected, e.g. which required feature it lacks.
    std::string reasons;
    for (const std::string &reason : result.detailed_failure_reasons()) {
      reasons += std::format("\n  {}", reason);
    }
    throw core::Exception{std::format("{}: {}{}", what, result.error().message(), reasons),
                          core::ErrorCategory::Graphics};
  }
  return std::move(result.value());
}

template <typename T> std::uint64_t objectHandle(T object) {
  return reinterpret_cast<std::uint64_t>(static_cast<typename T::NativeType>(object));
}

} // namespace

std::uint32_t VulkanDevice::IndexAllocator::allocate(std::string_view what) {
  if (!free.empty()) {
    const std::uint32_t index = free.back();
    free.pop_back();
    return index;
  }
  if (next >= capacity) {
    SONNET_LOG_ERROR("bindless {} array exhausted at {} entries", what, capacity);
    return InvalidBindlessIndex;
  }
  return next++;
}

void VulkanDevice::IndexAllocator::release(std::uint32_t index) {
  if (index != InvalidBindlessIndex) {
    free.push_back(index);
  }
}

VulkanDevice::VulkanDevice(const DeviceDesc &desc)
    : m_context(desc.platform->vulkanGetInstanceProcAddr()), m_commandList(*this) {
  SONNET_ASSERT(desc.platform != nullptr, "a device needs the platform for the Vulkan loader and surfaces");
  createInstance(desc);
  selectAndCreateDevice(desc);
  createAllocator();
  createPipelineLayout();
  createBindlessSet();
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
    if (frame.stagingBuffer) {
      m_buffers.remove(frame.stagingBuffer);
    }
  }
  reportLeaks();
  m_pipelines.clear();
  m_shaders.clear();
  m_samplers.clear();
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

void VulkanDevice::selectAndCreateDevice(const DeviceDesc &desc) {
  // The features in docs/rendering.md, "Vulkan baseline". Extended dynamic state is core in 1.3
  // without a feature bit.
  VkPhysicalDeviceFeatures features{};
  features.samplerAnisotropy = VK_TRUE;
  // The indirect draws of ADR-0012: many commands per call, each naming its object in
  // firstInstance.
  features.multiDrawIndirect = VK_TRUE;
  features.drawIndirectFirstInstance = VK_TRUE;

  VkPhysicalDeviceVulkan11Features features11{};
  features11.shaderDrawParameters = VK_TRUE; // Slang lowers SV_VertexID through gl_BaseVertex

  VkPhysicalDeviceVulkan12Features features12{};
  features12.timelineSemaphore = VK_TRUE;
  features12.bufferDeviceAddress = VK_TRUE;
  features12.scalarBlockLayout = VK_TRUE;
  features12.descriptorIndexing = VK_TRUE;
  features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
  features12.shaderStorageImageArrayNonUniformIndexing = VK_TRUE;
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
                                                  .set_required_features(features)
                                                  .set_required_features_11(features11)
                                                  .set_required_features_12(features12)
                                                  .set_required_features_13(features13)
                                                  .set_required_features_14(features14)
                                                  .select(),
                                              "selecting a Vulkan 1.4 device");
  physicalDevice.enable_extension_if_present(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);

  // Block compression is what cooked textures use on desktop; mobile GPUs bring ASTC instead
  // (docs/assets.md, "Textures"), so it is enabled where present rather than required.
  VkPhysicalDeviceFeatures optional{};
  optional.textureCompressionBC = VK_TRUE;
  m_info.blockCompressionSupported = physicalDevice.enable_features_if_present(optional);
  // The count form of the indirect draws (ADR-0012) is enabled where present rather than
  // required: MoltenVK has no drawIndirectCount, and the renderer draws every slot there instead
  // (ADR-0014).
  if (!desc.disableDrawIndirectCount) {
    VkPhysicalDeviceVulkan12Features count{};
    count.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    count.drawIndirectCount = VK_TRUE;
    m_info.drawIndirectCountSupported = physicalDevice.enable_extension_features_if_present(count);
  }

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
  m_maxAnisotropy = limits.maxSamplerAnisotropy;
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
  // Set 0 is the bindless set: runtime arrays that are partially bound and updated after bind,
  // so a resource created mid-frame is written into a free slot at once. Set 1 takes the
  // per-pass buffers and one sampled image as push descriptors (docs/rendering.md, "Frame
  // structure"). Both sets and the push constants are visible to every stage, since compute
  // passes share the layout.
  const std::array bindlessBindings{
      vk::DescriptorSetLayoutBinding{BindlessSampledImageBinding, vk::DescriptorType::eSampledImage,
                                     MaxBindlessSampledImages, vk::ShaderStageFlagBits::eAll},
      vk::DescriptorSetLayoutBinding{BindlessSamplerBinding, vk::DescriptorType::eSampler, MaxBindlessSamplers,
                                     vk::ShaderStageFlagBits::eAll},
      vk::DescriptorSetLayoutBinding{BindlessStorageImageBinding, vk::DescriptorType::eStorageImage,
                                     MaxBindlessStorageImages, vk::ShaderStageFlagBits::eAll},
      vk::DescriptorSetLayoutBinding{BindlessCubeImageBinding, vk::DescriptorType::eSampledImage, MaxBindlessCubeImages,
                                     vk::ShaderStageFlagBits::eAll},
      vk::DescriptorSetLayoutBinding{BindlessComparisonSamplerBinding, vk::DescriptorType::eSampler,
                                     MaxBindlessComparisonSamplers, vk::ShaderStageFlagBits::eAll},
  };
  constexpr vk::DescriptorBindingFlags bindingFlags =
      vk::DescriptorBindingFlagBits::ePartiallyBound | vk::DescriptorBindingFlagBits::eUpdateAfterBind;
  std::array<vk::DescriptorBindingFlags, bindlessBindings.size()> flags;
  flags.fill(bindingFlags);
  const vk::DescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{flags};
  m_bindlessLayout = vk::raii::DescriptorSetLayout{
      m_device, vk::DescriptorSetLayoutCreateInfo{vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool,
                                                  bindlessBindings, &flagsInfo}};

  const std::array passBindings{
      vk::DescriptorSetLayoutBinding{PassUniformBinding, vk::DescriptorType::eUniformBuffer, 1,
                                     vk::ShaderStageFlagBits::eAll},
      vk::DescriptorSetLayoutBinding{PassStorageBinding, vk::DescriptorType::eStorageBuffer, 1,
                                     vk::ShaderStageFlagBits::eAll},
      vk::DescriptorSetLayoutBinding{PassImageBinding, vk::DescriptorType::eSampledImage, 1,
                                     vk::ShaderStageFlagBits::eAll},
  };
  m_passLayout = vk::raii::DescriptorSetLayout{
      m_device,
      vk::DescriptorSetLayoutCreateInfo{vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptor, passBindings}};
  const std::array layouts{*m_bindlessLayout, *m_passLayout};
  const vk::PushConstantRange range{vk::ShaderStageFlagBits::eAll, 0, PushConstantSize};
  m_pipelineLayout = vk::raii::PipelineLayout{m_device, vk::PipelineLayoutCreateInfo{{}, layouts, range}};
  setDebugName(vk::ObjectType::eDescriptorSetLayout, objectHandle(*m_bindlessLayout), "bindless set layout");
  setDebugName(vk::ObjectType::eDescriptorSetLayout, objectHandle(*m_passLayout), "pass set layout");
  setDebugName(vk::ObjectType::ePipelineLayout, objectHandle(*m_pipelineLayout), "shared pipeline layout");
}

void VulkanDevice::createBindlessSet() {
  const std::array sizes{
      vk::DescriptorPoolSize{vk::DescriptorType::eSampledImage, MaxBindlessSampledImages + MaxBindlessCubeImages},
      vk::DescriptorPoolSize{vk::DescriptorType::eSampler, MaxBindlessSamplers + MaxBindlessComparisonSamplers},
      vk::DescriptorPoolSize{vk::DescriptorType::eStorageImage, MaxBindlessStorageImages},
  };
  // The set is a RAII object that frees itself, which the pool has to allow.
  m_bindlessPool = vk::raii::DescriptorPool{
      m_device, vk::DescriptorPoolCreateInfo{vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind |
                                                 vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
                                             1, sizes}};
  vk::raii::DescriptorSets sets{m_device, vk::DescriptorSetAllocateInfo{*m_bindlessPool, 1, &*m_bindlessLayout}};
  m_bindlessSet = std::move(sets.front());
  setDebugName(vk::ObjectType::eDescriptorPool, objectHandle(*m_bindlessPool), "bindless pool");
  setDebugName(vk::ObjectType::eDescriptorSet, objectHandle(*m_bindlessSet), "bindless set");
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
    frame.uploadPool = vk::raii::CommandPool{m_device, vk::CommandPoolCreateInfo{{}, m_graphicsFamily}};
    vk::raii::CommandBuffers uploadBuffers{
        m_device, vk::CommandBufferAllocateInfo{*frame.uploadPool, vk::CommandBufferLevel::ePrimary, 1}};
    frame.uploadCommandBuffer = std::move(uploadBuffers.front());
    frame.imageAvailable = vk::raii::Semaphore{m_device, vk::SemaphoreCreateInfo{}};
    setDebugName(vk::ObjectType::eCommandPool, objectHandle(*frame.commandPool),
                 std::format("frame {} command pool", i));
    setDebugName(vk::ObjectType::eCommandPool, objectHandle(*frame.uploadPool), std::format("frame {} upload pool", i));
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
    frame.stagingBuffer = createBuffer({.size = StagingBufferSize,
                                        .usage = BufferUsage::TransferSrc,
                                        .memory = MemoryUsage::CpuToGpu,
                                        .debugName = std::format("frame {} staging", i)});
  }
}

std::unique_ptr<ISwapchain> VulkanDevice::createSwapchain(platform::IWindow &window) {
  assertOwnerThread("createSwapchain");
  return std::make_unique<VulkanSwapchain>(*this, window);
}

BufferHandle VulkanDevice::createBuffer(const BufferDesc &desc) {
  assertOwnerThread("createBuffer");
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
  assertOwnerThread("destroyBuffer");
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

void VulkanDevice::writeSampledDescriptor(std::uint32_t binding, std::uint32_t index, vk::ImageView view,
                                          vk::ImageLayout layout) {
  if (index == InvalidBindlessIndex) {
    return;
  }
  const vk::DescriptorImageInfo info{nullptr, view, layout};
  const vk::DescriptorType type =
      binding == BindlessStorageImageBinding ? vk::DescriptorType::eStorageImage : vk::DescriptorType::eSampledImage;
  m_device.updateDescriptorSets(vk::WriteDescriptorSet{*m_bindlessSet, binding, index, 1, type, &info}, {});
}

void VulkanDevice::writeSamplerDescriptor(std::uint32_t binding, std::uint32_t index, vk::Sampler sampler) {
  if (index == InvalidBindlessIndex) {
    return;
  }
  const vk::DescriptorImageInfo info{sampler, nullptr, vk::ImageLayout::eUndefined};
  m_device.updateDescriptorSets(
      vk::WriteDescriptorSet{*m_bindlessSet, binding, index, 1, vk::DescriptorType::eSampler, &info}, {});
}

ImageHandle VulkanDevice::createImage(const ImageDesc &desc) {
  assertOwnerThread("createImage");
  SONNET_ASSERT(desc.size.x > 0 && desc.size.y > 0, "image \"{}\" has no size", desc.debugName);
  SONNET_ASSERT(desc.mipLevels >= 1 && desc.mipLevels <= fullMipCount(desc.size), "image \"{}\": {} mip levels",
                desc.debugName, desc.mipLevels);
  SONNET_ASSERT(!desc.cube || desc.size.x == desc.size.y, "cube image \"{}\" is not square", desc.debugName);
  vk::ImageCreateInfo imageInfo{desc.cube ? vk::ImageCreateFlagBits::eCubeCompatible : vk::ImageCreateFlags{},
                                vk::ImageType::e2D,
                                toVk(desc.format),
                                vk::Extent3D{desc.size.x, desc.size.y, 1},
                                desc.mipLevels,
                                desc.layers(),
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
                                   desc.cube ? vk::ImageViewType::eCube : vk::ImageViewType::e2D,
                                   toVk(desc.format),
                                   {},
                                   wholeImage(desc.format)};
  vk::raii::ImageView view{m_device, viewInfo};
  setDebugName(vk::ObjectType::eImageView, objectHandle(*view), std::format("{} view", desc.debugName));

  const vk::Image imageHandle = *image;
  const ImageHandle handle = m_images.emplace(
      VulkanImage{desc, std::move(allocation), std::move(image), imageHandle, std::move(view), {}, {}});
  VulkanImage &resource = m_images.get(handle);
  if (has(desc.usage, ImageUsage::Sampled)) {
    resource.sampledIndex = desc.cube ? m_cubeIndices.allocate("cube image") : m_sampledIndices.allocate("image");
    writeSampledDescriptor(desc.cube ? BindlessCubeImageBinding : BindlessSampledImageBinding, resource.sampledIndex,
                           *resource.view, vk::ImageLayout::eShaderReadOnlyOptimal);
  }
  SONNET_LOG_TRACE("image \"{}\" {}x{} -> {}:{}", desc.debugName, desc.size.x, desc.size.y, handle.index,
                   handle.generation);
  return handle;
}

ImageHandle VulkanDevice::registerExternalImage(vk::Image image, const ImageDesc &desc) {
  assertOwnerThread("registerExternalImage");
  vk::ImageViewCreateInfo viewInfo{{}, image, vk::ImageViewType::e2D, toVk(desc.format), {}, wholeImage(desc.format)};
  vk::raii::ImageView view{m_device, viewInfo};
  setDebugName(vk::ObjectType::eImage, objectHandle(image), desc.debugName);
  setDebugName(vk::ObjectType::eImageView, objectHandle(*view), std::format("{} view", desc.debugName));
  return m_images.emplace(VulkanImage{desc, {}, {}, image, std::move(view), {}, {}});
}

void VulkanDevice::destroyImage(ImageHandle handle) {
  assertOwnerThread("destroyImage");
  std::optional<VulkanImage> image = m_images.remove(handle);
  if (!image) {
    SONNET_LOG_WARN("destroyImage: stale handle {}:{}", handle.index, handle.generation);
    return;
  }
  // The bindless slots are reused only once the GPU is past every frame that could read them.
  deferDestruction([this, resource = std::make_shared<VulkanImage>(std::move(*image))]() mutable {
    if (resource->desc.cube) {
      m_cubeIndices.release(resource->sampledIndex);
    } else {
      m_sampledIndices.release(resource->sampledIndex);
    }
    for (const std::uint32_t index : resource->storageIndices) {
      m_storageIndices.release(index);
    }
    resource.reset();
  });
}

const ImageDesc &VulkanDevice::imageDesc(ImageHandle handle) const {
  return m_images.get(handle).desc;
}

std::uint32_t VulkanDevice::sampledImageIndex(ImageHandle handle) const {
  const VulkanImage *image = m_images.find(handle);
  return image != nullptr ? image->sampledIndex : InvalidBindlessIndex;
}

std::uint32_t VulkanDevice::storageImageIndex(ImageHandle handle, std::uint32_t mipLevel) {
  assertOwnerThread("storageImageIndex");
  VulkanImage *image = m_images.find(handle);
  if (image == nullptr || !has(image->desc.usage, ImageUsage::Storage) || mipLevel >= image->desc.mipLevels) {
    SONNET_LOG_WARN("storageImageIndex: no storage view for level {} of image {}:{}", mipLevel, handle.index,
                    handle.generation);
    return InvalidBindlessIndex;
  }
  if (image->storageViews.empty()) {
    image->storageViews.reserve(image->desc.mipLevels);
    for (std::uint32_t level = 0; level < image->desc.mipLevels; ++level) {
      image->storageViews.emplace_back(nullptr);
    }
    image->storageIndices.assign(image->desc.mipLevels, InvalidBindlessIndex);
  }
  if (image->storageIndices[mipLevel] == InvalidBindlessIndex) {
    // Storage views are 2D arrays whatever the image, so one shader declaration covers plain
    // images (one layer) and cube maps (six).
    vk::ImageViewCreateInfo viewInfo{
        {},
        image->image,
        vk::ImageViewType::e2DArray,
        toVk(image->desc.format),
        {},
        vk::ImageSubresourceRange{aspectOf(image->desc.format), mipLevel, 1, 0, image->desc.layers()}};
    image->storageViews[mipLevel] = vk::raii::ImageView{m_device, viewInfo};
    setDebugName(vk::ObjectType::eImageView, objectHandle(*image->storageViews[mipLevel]),
                 std::format("{} storage view {}", image->desc.debugName, mipLevel));
    image->storageIndices[mipLevel] = m_storageIndices.allocate("storage image");
    writeSampledDescriptor(BindlessStorageImageBinding, image->storageIndices[mipLevel], *image->storageViews[mipLevel],
                           vk::ImageLayout::eGeneral);
  }
  return image->storageIndices[mipLevel];
}

SamplerHandle VulkanDevice::createSampler(const SamplerDesc &desc) {
  assertOwnerThread("createSampler");
  const bool anisotropic = desc.anisotropy > 1.0f;
  const vk::SamplerCreateInfo info{{},
                                   toVk(desc.filter),
                                   toVk(desc.filter),
                                   toVkMipmapMode(desc.mipFilter),
                                   toVk(desc.addressMode),
                                   toVk(desc.addressMode),
                                   toVk(desc.addressMode),
                                   0.0f,
                                   anisotropic ? VK_TRUE : VK_FALSE,
                                   std::min(desc.anisotropy, m_maxAnisotropy),
                                   desc.compare ? VK_TRUE : VK_FALSE,
                                   vk::CompareOp::eGreaterOrEqual,
                                   0.0f,
                                   vk::LodClampNone,
                                   vk::BorderColor::eFloatOpaqueWhite,
                                   VK_FALSE};
  vk::raii::Sampler sampler{m_device, info};
  setDebugName(vk::ObjectType::eSampler, objectHandle(*sampler), desc.debugName);
  const std::uint32_t index =
      desc.compare ? m_comparisonSamplerIndices.allocate("comparison sampler") : m_samplerIndices.allocate("sampler");
  writeSamplerDescriptor(desc.compare ? BindlessComparisonSamplerBinding : BindlessSamplerBinding, index, *sampler);
  const SamplerHandle handle = m_samplers.emplace(VulkanSampler{desc, std::move(sampler), index});
  SONNET_LOG_TRACE("sampler \"{}\" -> {}:{} at index {}", desc.debugName, handle.index, handle.generation, index);
  return handle;
}

void VulkanDevice::destroySampler(SamplerHandle handle) {
  assertOwnerThread("destroySampler");
  std::optional<VulkanSampler> sampler = m_samplers.remove(handle);
  if (!sampler) {
    SONNET_LOG_WARN("destroySampler: stale handle {}:{}", handle.index, handle.generation);
    return;
  }
  deferDestruction([this, resource = std::make_shared<VulkanSampler>(std::move(*sampler))]() mutable {
    if (resource->desc.compare) {
      m_comparisonSamplerIndices.release(resource->index);
    } else {
      m_samplerIndices.release(resource->index);
    }
    resource.reset();
  });
}

std::uint32_t VulkanDevice::samplerIndex(SamplerHandle handle) const {
  const VulkanSampler *sampler = m_samplers.find(handle);
  return sampler != nullptr ? sampler->index : InvalidBindlessIndex;
}

void VulkanDevice::prepareSlot() {
  Frame &frame = m_frames[m_frameIndex];
  if (frame.prepared) {
    return;
  }
  waitForFrame(frame);
  readTimestamps(frame);
  // The pools go back before the garbage does: a command buffer that names a resource counts as
  // using it, so the slot's own command buffers are returned to the initial state first.
  frame.commandPool.reset();
  frame.uploadPool.reset();
  for (auto &destroy : frame.garbage) {
    destroy();
  }
  frame.garbage.clear();
  frame.transientOffset = 0;
  frame.transientExhausted = false;
  frame.stagingOffset = 0;
  frame.uploadsRecorded = false;
  frame.timestampCount = 0;
  frame.prepared = true;
}

vk::CommandBuffer VulkanDevice::uploadCommands() {
  prepareSlot();
  Frame &frame = m_frames[m_frameIndex];
  const vk::CommandBuffer commands = *frame.uploadCommandBuffer;
  if (!frame.uploadsRecorded) {
    const vk::CommandBufferBeginInfo begin{vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
    m_device.getDispatcher()->vkBeginCommandBuffer(commands,
                                                   reinterpret_cast<const VkCommandBufferBeginInfo *>(&begin));
    // The previous frame may still read a buffer this upload overwrites.
    const vk::MemoryBarrier2 before{vk::PipelineStageFlagBits2::eAllCommands,
                                    vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
                                    vk::PipelineStageFlagBits2::eAllTransfer, vk::AccessFlagBits2::eTransferWrite};
    const vk::DependencyInfo dependency{{}, 1, &before};
    m_device.getDispatcher()->vkCmdPipelineBarrier2(commands, reinterpret_cast<const VkDependencyInfo *>(&dependency));
    frame.uploadsRecorded = true;
  }
  return commands;
}

VulkanDevice::Staging VulkanDevice::stage(std::uint64_t size, std::string_view what) {
  Frame &frame = m_frames[m_frameIndex];
  const std::uint64_t offset = (frame.stagingOffset + StagingAlignment - 1) / StagingAlignment * StagingAlignment;
  if (offset + size <= StagingBufferSize) {
    frame.stagingOffset = offset + size;
    const VulkanBuffer &ring = m_buffers.get(frame.stagingBuffer);
    return Staging{*ring.buffer, offset, ring.mapped + offset};
  }
  // Too large for the ring: a buffer of its own, released with the slot's garbage after the
  // frame that carries the upload has completed.
  SONNET_LOG_DEBUG("upload of {} bytes for \"{}\" exceeds the staging ring, using a dedicated buffer", size, what);
  vk::BufferCreateInfo bufferInfo{{}, size, vk::BufferUsageFlagBits::eTransferSrc, vk::SharingMode::eExclusive};
  vma::AllocationCreateInfo allocationInfo{};
  allocationInfo.usage = vma::MemoryUsage::eAuto;
  allocationInfo.flags =
      vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;
  vma::AllocationInfo result{};
  auto [allocation, buffer] = m_allocator->createBufferUnique(bufferInfo, allocationInfo, result);
  setDebugName(vk::ObjectType::eBuffer, objectHandle(*buffer), std::format("{} staging", what));
  const Staging staging{*buffer, 0, static_cast<std::byte *>(result.pMappedData)};
  frame.garbage.push_back(
      [dedicated = std::make_shared<VulkanBuffer>(
           VulkanBuffer{{}, std::move(allocation), std::move(buffer), nullptr, 0})]() mutable { dedicated.reset(); });
  return staging;
}

void VulkanDevice::uploadBuffer(BufferHandle handle, std::uint64_t offset, std::span<const std::byte> data) {
  assertOwnerThread("uploadBuffer");
  const VulkanBuffer *buffer = m_buffers.find(handle);
  if (buffer == nullptr) {
    SONNET_LOG_WARN("uploadBuffer: stale handle {}:{}", handle.index, handle.generation);
    return;
  }
  SONNET_ASSERT(has(buffer->desc.usage, BufferUsage::TransferDst), "buffer \"{}\" is not a transfer destination",
                buffer->desc.debugName);
  SONNET_ASSERT(offset + data.size() <= buffer->desc.size, "upload of {} bytes at {} overflows buffer \"{}\"",
                data.size(), offset, buffer->desc.debugName);
  if (data.empty()) {
    return;
  }
  const vk::CommandBuffer commands = uploadCommands();
  const Staging staging = stage(data.size(), buffer->desc.debugName);
  std::memcpy(staging.mapped, data.data(), data.size());
  const vk::BufferCopy region{staging.offset, offset, data.size()};
  m_device.getDispatcher()->vkCmdCopyBuffer(commands, staging.buffer, *buffer->buffer, 1,
                                            reinterpret_cast<const VkBufferCopy *>(&region));
}

void VulkanDevice::uploadImage(ImageHandle handle, std::span<const ImageUpload> uploads) {
  assertOwnerThread("uploadImage");
  const VulkanImage *image = m_images.find(handle);
  if (image == nullptr) {
    SONNET_LOG_WARN("uploadImage: stale handle {}:{}", handle.index, handle.generation);
    return;
  }
  const ImageDesc &desc = image->desc;
  SONNET_ASSERT(has(desc.usage, ImageUsage::TransferDst), "image \"{}\" is not a transfer destination", desc.debugName);
  const vk::CommandBuffer commands = uploadCommands();
  const vk::ImageMemoryBarrier2 toTransfer{vk::PipelineStageFlagBits2::eAllCommands,
                                           vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
                                           vk::PipelineStageFlagBits2::eAllTransfer,
                                           vk::AccessFlagBits2::eTransferWrite,
                                           vk::ImageLayout::eUndefined,
                                           vk::ImageLayout::eTransferDstOptimal,
                                           vk::QueueFamilyIgnored,
                                           vk::QueueFamilyIgnored,
                                           image->image,
                                           wholeImage(desc.format)};
  const vk::DependencyInfo before{{}, 0, nullptr, 0, nullptr, 1, &toTransfer};
  m_device.getDispatcher()->vkCmdPipelineBarrier2(commands, reinterpret_cast<const VkDependencyInfo *>(&before));
  for (const ImageUpload &upload : uploads) {
    SONNET_ASSERT(upload.mipLevel < desc.mipLevels && upload.layer < desc.layers(),
                  "upload to level {} layer {} of image \"{}\"", upload.mipLevel, upload.layer, desc.debugName);
    const glm::uvec2 size = mipSize(desc.size, upload.mipLevel);
    const std::uint64_t expected = levelByteSize(desc.format, size);
    SONNET_ASSERT(upload.data.size() == expected, "level {} of image \"{}\" takes {} bytes, {} given", upload.mipLevel,
                  desc.debugName, expected, upload.data.size());
    const Staging staging = stage(upload.data.size(), desc.debugName);
    std::memcpy(staging.mapped, upload.data.data(), upload.data.size());
    const vk::BufferImageCopy region{
        staging.offset,
        0,
        0,
        vk::ImageSubresourceLayers{aspectOf(desc.format), upload.mipLevel, upload.layer, 1},
        vk::Offset3D{0, 0, 0},
        vk::Extent3D{size.x, size.y, 1}};
    m_device.getDispatcher()->vkCmdCopyBufferToImage(commands, staging.buffer, image->image,
                                                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                                                     reinterpret_cast<const VkBufferImageCopy *>(&region));
  }
  const vk::ImageMemoryBarrier2 toRead{vk::PipelineStageFlagBits2::eAllTransfer,
                                       vk::AccessFlagBits2::eTransferWrite,
                                       vk::PipelineStageFlagBits2::eAllCommands,
                                       vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eMemoryRead,
                                       vk::ImageLayout::eTransferDstOptimal,
                                       vk::ImageLayout::eShaderReadOnlyOptimal,
                                       vk::QueueFamilyIgnored,
                                       vk::QueueFamilyIgnored,
                                       image->image,
                                       wholeImage(desc.format)};
  const vk::DependencyInfo after{{}, 0, nullptr, 0, nullptr, 1, &toRead};
  m_device.getDispatcher()->vkCmdPipelineBarrier2(commands, reinterpret_cast<const VkDependencyInfo *>(&after));
}

ShaderHandle VulkanDevice::createShader(const ShaderDesc &desc) {
  assertOwnerThread("createShader");
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
  assertOwnerThread("destroyShader");
  std::optional<VulkanShader> shader = m_shaders.remove(handle);
  if (!shader) {
    SONNET_LOG_WARN("destroyShader: stale handle {}:{}", handle.index, handle.generation);
    return;
  }
  // Modules are not referenced by submitted work; pipelines hold what they need.
  shader.reset();
}

PipelineHandle VulkanDevice::createGraphicsPipeline(const GraphicsPipelineDesc &desc) {
  assertOwnerThread("createGraphicsPipeline");
  const VulkanShader *shader = m_shaders.find(desc.shader);
  if (shader == nullptr) {
    throw core::Exception{std::format("pipeline \"{}\": stale shader handle", desc.debugName),
                          core::ErrorCategory::Graphics};
  }
  std::vector<vk::PipelineShaderStageCreateInfo> stages;
  stages.emplace_back(vk::PipelineShaderStageCreateFlags{}, vk::ShaderStageFlagBits::eVertex, *shader->module,
                      desc.vertexEntry.c_str());
  if (!desc.fragmentEntry.empty()) {
    stages.emplace_back(vk::PipelineShaderStageCreateFlags{}, vk::ShaderStageFlagBits::eFragment, *shader->module,
                        desc.fragmentEntry.c_str());
  }
  // No vertex input: vertex data is pulled from buffers by index (docs/rendering.md).
  const vk::PipelineVertexInputStateCreateInfo vertexInput{};
  const vk::PipelineInputAssemblyStateCreateInfo inputAssembly{
      {},
      desc.topology == Topology::LineList ? vk::PrimitiveTopology::eLineList : vk::PrimitiveTopology::eTriangleList,
      VK_FALSE};
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
  const std::vector<vk::PipelineColorBlendAttachmentState> blendAttachments(desc.colorFormats.size(), toVk(desc.blend));
  const vk::PipelineColorBlendStateCreateInfo colorBlend{{}, VK_FALSE, vk::LogicOp::eCopy, blendAttachments};
  // The front face is dynamic so a mirrored draw can flip it without a pipeline of its own; the
  // rasterization state's value is ignored and bindPipeline sets counter-clockwise.
  const std::array dynamicStates{vk::DynamicState::eViewport, vk::DynamicState::eScissor, vk::DynamicState::eFrontFace};
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
  return m_pipelines.emplace(VulkanPipeline{desc.debugName, std::move(pipeline), false});
}

PipelineHandle VulkanDevice::createComputePipeline(const ComputePipelineDesc &desc) {
  assertOwnerThread("createComputePipeline");
  const VulkanShader *shader = m_shaders.find(desc.shader);
  if (shader == nullptr) {
    throw core::Exception{std::format("compute pipeline \"{}\": stale shader handle", desc.debugName),
                          core::ErrorCategory::Graphics};
  }
  const vk::PipelineShaderStageCreateInfo stage{
      {}, vk::ShaderStageFlagBits::eCompute, *shader->module, desc.entry.c_str()};
  const vk::ComputePipelineCreateInfo info{{}, stage, *m_pipelineLayout};
  vk::raii::Pipeline pipeline{m_device, nullptr, info};
  setDebugName(vk::ObjectType::ePipeline, objectHandle(*pipeline), desc.debugName);
  SONNET_LOG_DEBUG("compute pipeline \"{}\" from shader \"{}\"", desc.debugName, shader->debugName);
  return m_pipelines.emplace(VulkanPipeline{desc.debugName, std::move(pipeline), true});
}

void VulkanDevice::destroyPipeline(PipelineHandle handle) {
  assertOwnerThread("destroyPipeline");
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

bool VulkanDevice::isValid(SamplerHandle handle) const {
  return m_samplers.contains(handle);
}

void VulkanDevice::assertOwnerThread(std::string_view what) const {
  detail::assertOwnerThread(m_ownerThread, what);
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
  assertOwnerThread("beginFrame");
  SONNET_ZONE();
  SONNET_ASSERT(!m_recording, "beginFrame called twice without endFrame");
  prepareSlot();
  Frame &frame = m_frames[m_frameIndex];
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
  assertOwnerThread("allocateTransient");
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
  assertOwnerThread("endFrame");
  SONNET_ZONE();
  SONNET_ASSERT(m_recording, "endFrame called without beginFrame");
  Frame &frame = m_frames[m_frameIndex];
  m_commandList.end();
  m_recording = false;

  // Small fixed-capacity arrays: one swapchain per window, and the frame never allocates here
  // for the common single-window case.
  std::vector<vk::CommandBufferSubmitInfo> commandInfos;
  commandInfos.reserve(2);
  if (frame.uploadsRecorded) {
    // Buffer uploads become visible to everything after the copies; image uploads carry their
    // own transitions.
    const vk::CommandBuffer uploads = *frame.uploadCommandBuffer;
    const vk::MemoryBarrier2 after{vk::PipelineStageFlagBits2::eAllTransfer, vk::AccessFlagBits2::eTransferWrite,
                                   vk::PipelineStageFlagBits2::eAllCommands,
                                   vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite};
    const vk::DependencyInfo dependency{{}, 1, &after};
    m_device.getDispatcher()->vkCmdPipelineBarrier2(uploads, reinterpret_cast<const VkDependencyInfo *>(&dependency));
    m_device.getDispatcher()->vkEndCommandBuffer(uploads);
    // Recorded and now submitted: this slot has nothing pending until it is prepared again.
    frame.uploadsRecorded = false;
    commandInfos.emplace_back(uploads);
  }
  commandInfos.emplace_back(*frame.commandBuffer);

  const std::uint64_t signalValue = ++m_timelineValue;
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

  const vk::SubmitInfo2 submit{{}, waits, commandInfos, signals};
  const VkResult submitResult = m_device.getDispatcher()->vkQueueSubmit2(
      *m_graphicsQueue, 1, reinterpret_cast<const VkSubmitInfo2 *>(&submit), VK_NULL_HANDLE);
  if (submitResult != VK_SUCCESS) {
    SONNET_LOG_CRITICAL("vkQueueSubmit2 failed: {}", vk::to_string(static_cast<vk::Result>(submitResult)));
  }
  frame.submittedValue = signalValue;
  frame.prepared = false;

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

void VulkanDevice::submitRecordedUploads() {
  Frame &frame = m_frames[m_frameIndex];
  if (m_recording || !frame.uploadsRecorded) {
    return;
  }
  const vk::CommandBuffer uploads = *frame.uploadCommandBuffer;
  const vk::MemoryBarrier2 after{vk::PipelineStageFlagBits2::eAllTransfer, vk::AccessFlagBits2::eTransferWrite,
                                 vk::PipelineStageFlagBits2::eAllCommands,
                                 vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite};
  const vk::DependencyInfo dependency{{}, 1, &after};
  m_device.getDispatcher()->vkCmdPipelineBarrier2(uploads, reinterpret_cast<const VkDependencyInfo *>(&dependency));
  m_device.getDispatcher()->vkEndCommandBuffer(uploads);
  frame.uploadsRecorded = false;

  const std::uint64_t signalValue = ++m_timelineValue;
  const vk::CommandBufferSubmitInfo commandInfo{uploads};
  const vk::SemaphoreSubmitInfo signal{*m_timeline, signalValue, vk::PipelineStageFlagBits2::eAllCommands};
  const vk::SubmitInfo2 submit{{}, {}, commandInfo, signal};
  const VkResult result = m_device.getDispatcher()->vkQueueSubmit2(
      *m_graphicsQueue, 1, reinterpret_cast<const VkSubmitInfo2 *>(&submit), VK_NULL_HANDLE);
  if (result != VK_SUCCESS) {
    SONNET_LOG_ERROR("vkQueueSubmit2 of pending uploads failed: {}", vk::to_string(static_cast<vk::Result>(result)));
    return;
  }
  frame.submittedValue = signalValue;
}

void VulkanDevice::waitIdle() {
  assertOwnerThread("waitIdle");
  if (*m_device == nullptr) {
    return;
  }
  // Uploads staged since the last endFrame are still recorded in this slot's command buffer, and
  // a caller that waits for the device to go idle usually means to tear something down next.
  // Submitting them keeps the data and leaves nothing holding a reference to it.
  submitRecordedUploads();
  m_device.waitIdle();
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
  m_samplers.forEach([](SamplerHandle handle, VulkanSampler &sampler) {
    SONNET_LOG_WARN("leaked sampler \"{}\" ({}:{})", sampler.desc.debugName, handle.index, handle.generation);
  });
  m_shaders.forEach([](ShaderHandle handle, VulkanShader &shader) {
    SONNET_LOG_WARN("leaked shader \"{}\" ({}:{})", shader.debugName, handle.index, handle.generation);
  });
  m_pipelines.forEach([](PipelineHandle handle, VulkanPipeline &pipeline) {
    SONNET_LOG_WARN("leaked pipeline \"{}\" ({}:{})", pipeline.debugName, handle.index, handle.generation);
  });
}

} // namespace sonnet::rhi
