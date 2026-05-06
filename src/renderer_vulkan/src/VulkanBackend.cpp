#include "VulkanBackend.hpp"

#include <sonnet/logging/Logger.hpp>
#include <sonnet/renderer/CPUMesh.hpp>
#include <sonnet/renderer/VulkanBackendFactory.hpp>
#include <sonnet/window/IWindow.hpp>

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>

#ifdef SONNET_VALIDATION_LAYERS_ENABLED
constexpr bool kValidationEnabled = true;
#else
constexpr bool kValidationEnabled = false;
#endif

static const std::vector<const char*> kValidationLayers = {"VK_LAYER_KHRONOS_validation"};

static const std::vector<const char*> kDeviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

static constexpr float kClearColorComponent = 0.05F;

namespace sonnet::renderer {

// ─── Debug callback ───────────────────────────────────────────────────────────

VKAPI_ATTR VkBool32 VKAPI_CALL
VulkanBackend::debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                             VkDebugUtilsMessageTypeFlagsEXT /*messageType*/,
                             const VkDebugUtilsMessengerCallbackDataEXT* data, void* /*userData*/) {
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        SONNET_LOG_ERROR("Vulkan: {}", data->pMessage);
    } else {
        SONNET_LOG_WARN("Vulkan: {}", data->pMessage);
    }
    return VK_FALSE;
}

// ─── Lifecycle ────────────────────────────────────────────────────────────────

VulkanBackend::~VulkanBackend() {
    // cppcheck-suppress virtualCallInConstructor
    shutdown(); // NOLINT(clang-analyzer-optin.cplusplus.VirtualCall)
}

bool VulkanBackend::init(sonnet::window::IWindow& window) {
    m_window = &window;

    try {
        m_exeDir = std::filesystem::canonical("/proc/self/exe").parent_path().string();
    } catch (...) {
        m_exeDir = ".";
    }

    auto extensions = window.getRequiredInstanceExtensions();
    if (!createInstance(extensions)) {
        return false;
    }
    if (!createSurface(window)) {
        return false;
    }
    if (!selectPhysicalDevice()) {
        return false;
    }
    if (!createLogicalDevice()) {
        return false;
    }
    if (!createSwapchain(window)) {
        return false;
    }
    if (!createRenderPass()) {
        return false;
    }
    if (!createPipeline(m_exeDir)) {
        return false;
    }
    if (!createFramebuffers()) {
        return false;
    }
    if (!createCommandPool()) {
        return false;
    }
    if (!createSyncObjects()) {
        return false;
    }
    if (!createImguiDescriptorPool()) {
        return false;
    }
    if (!createOffscreenResources()) {
        return false;
    }
    if (!createForwardPipeline()) {
        return false;
    }
    if (!createPickingResources()) {
        SONNET_LOG_ERROR("GPU picking unavailable");
    }
    if (!createOutlineResources()) {
        SONNET_LOG_ERROR("Selection outline unavailable");
    }

    m_initialized = true;
    SONNET_LOG_INFO("Renderer initialized");
    return true;
}

void VulkanBackend::shutdown() {
    if (!m_initialized) {
        return;
    }
    m_initialized = false;

    try {
        m_device.waitIdle();

        flushPendingReleases();
        destroyOutlineResources();
        destroyPickingResources();
        destroyForwardResources();
        destroyOffscreenResources();

        if (m_imguiDescriptorPool) {
            m_device.destroyDescriptorPool(m_imguiDescriptorPool);
            m_imguiDescriptorPool = nullptr;
        }

        for (auto& sem : m_imageAvailableSemaphores) {
            m_device.destroySemaphore(sem);
        }
        for (auto& sem : m_renderFinishedSemaphores) {
            m_device.destroySemaphore(sem);
        }
        for (auto& fence : m_inFlightFences) {
            m_device.destroyFence(fence);
        }
        m_imageAvailableSemaphores.clear();
        m_renderFinishedSemaphores.clear();
        m_inFlightFences.clear();

        if (m_commandPool) {
            m_device.destroyCommandPool(m_commandPool);
            m_commandPool = nullptr;
        }

        destroySwapchainResources();

        if (m_pipeline) {
            m_device.destroyPipeline(m_pipeline);
            m_pipeline = nullptr;
        }
        if (m_pipelineLayout) {
            m_device.destroyPipelineLayout(m_pipelineLayout);
            m_pipelineLayout = nullptr;
        }
        if (m_renderPass) {
            m_device.destroyRenderPass(m_renderPass);
            m_renderPass = nullptr;
        }

        if (m_swapchain) {
            m_device.destroySwapchainKHR(m_swapchain);
            m_swapchain = nullptr;
        }
        if (m_device) {
            m_device.destroy();
            m_device = nullptr;
        }
        if (m_surface) {
            m_instance.destroySurfaceKHR(m_surface);
            m_surface = nullptr;
        }

        if constexpr (kValidationEnabled) {
            if (m_debugMessenger) {
                auto destroyFn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                    vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT"));
                if (destroyFn != nullptr) {
                    destroyFn(m_instance, m_debugMessenger, nullptr);
                }
                m_debugMessenger = nullptr;
            }
        }

        if (m_instance) {
            m_instance.destroy();
            m_instance = nullptr;
        }
    } catch (const vk::SystemError& err) {
        SONNET_LOG_ERROR("Error during Vulkan shutdown: {}", err.what());
    }
}

// ─── Part 1: Instance ─────────────────────────────────────────────────────────

bool VulkanBackend::createInstance(const std::vector<std::string>& windowExtensions) {
    try {
        vk::ApplicationInfo appInfo{"Sonnet Engine", VK_MAKE_VERSION(0, 1, 0), "Sonnet",
                                    VK_MAKE_VERSION(0, 1, 0), VK_API_VERSION_1_0};

        std::vector<const char*> extensions;
        extensions.reserve(windowExtensions.size() + 1);
        for (const auto& ext : windowExtensions) {
            extensions.push_back(ext.c_str());
        }

        if constexpr (kValidationEnabled) {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        std::vector<const char*> layers;
        if constexpr (kValidationEnabled) {
            auto available = vk::enumerateInstanceLayerProperties();
            bool found = std::ranges::any_of(available, [](const auto& prop) {
                return std::string_view(prop.layerName.data()) == "VK_LAYER_KHRONOS_validation";
            });
            if (found) {
                layers = kValidationLayers;
            } else {
                SONNET_LOG_WARN("Validation layers requested but not available");
            }
        }

        vk::InstanceCreateInfo createInfo{};
        createInfo.setPApplicationInfo(&appInfo);
        createInfo.setPEnabledExtensionNames(extensions);
        createInfo.setPEnabledLayerNames(layers);

        m_instance = vk::createInstance(createInfo);
        SONNET_LOG_INFO("Vulkan instance created");

        if constexpr (kValidationEnabled) {
            if (!layers.empty()) {
                VkDebugUtilsMessengerCreateInfoEXT dbCI{};
                dbCI.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
                dbCI.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                       VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
                dbCI.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                   VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                   VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
                dbCI.pfnUserCallback = debugCallback;

                auto createFn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                    vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT"));
                if (createFn != nullptr) {
                    VkDebugUtilsMessengerEXT messenger{};
                    createFn(m_instance, &dbCI, nullptr, &messenger);
                    m_debugMessenger = vk::DebugUtilsMessengerEXT(messenger);
                }
            }
        }
    } catch (const vk::SystemError& err) {
        SONNET_LOG_ERROR("Failed to create Vulkan instance: {}", err.what());
        return false;
    }
    return true;
}

bool VulkanBackend::createSurface(sonnet::window::IWindow& window) {
    uint64_t surfaceHandle = window.createSurface(reinterpret_cast<uint64_t>(
        static_cast<VkInstance>(m_instance))); // NOLINT(performance-no-int-to-ptr)
    if (surfaceHandle == 0) {
        SONNET_LOG_ERROR("Failed to create Vulkan surface");
        return false;
    }
    m_surface = vk::SurfaceKHR(
        reinterpret_cast<VkSurfaceKHR>(surfaceHandle)); // NOLINT(performance-no-int-to-ptr)
    return true;
}

// ─── Part 2: Device + Swapchain ───────────────────────────────────────────────

bool VulkanBackend::selectPhysicalDevice() {
    try {
        auto devices = m_instance.enumeratePhysicalDevices();
        if (devices.empty()) {
            SONNET_LOG_ERROR("No Vulkan-capable GPU found");
            return false;
        }

        for (const auto& dev : devices) {
            auto queueFamilies = dev.getQueueFamilyProperties();
            int graphicsIdx = -1;
            int presentIdx = -1;

            for (int i = 0; i < static_cast<int>(queueFamilies.size()); ++i) {
                if (queueFamilies[i].queueFlags & vk::QueueFlagBits::eGraphics) {
                    graphicsIdx = i;
                }
                if (dev.getSurfaceSupportKHR(static_cast<uint32_t>(i), m_surface) != 0U) {
                    presentIdx = i;
                }
                if (graphicsIdx >= 0 && presentIdx >= 0) {
                    break;
                }
            }

            if (graphicsIdx < 0 || presentIdx < 0) {
                continue;
            }

            auto exts = dev.enumerateDeviceExtensionProperties();
            bool swapchainOk = std::ranges::any_of(exts, [](const auto& ext) {
                return std::string_view(ext.extensionName.data()) ==
                       VK_KHR_SWAPCHAIN_EXTENSION_NAME;
            });
            if (!swapchainOk) {
                continue;
            }

            m_physicalDevice = dev;
            m_graphicsFamily = static_cast<uint32_t>(graphicsIdx);
            m_presentFamily = static_cast<uint32_t>(presentIdx);

            auto props = dev.getProperties();
            SONNET_LOG_INFO("Selected GPU: {}", props.deviceName.data());
            return true;
        }

        SONNET_LOG_ERROR("No suitable GPU found");
        return false;
    } catch (const vk::SystemError& err) {
        SONNET_LOG_ERROR("Physical device selection failed: {}", err.what());
        return false;
    }
}

bool VulkanBackend::createLogicalDevice() {
    try {
        std::set<uint32_t> uniqueFamilies{m_graphicsFamily, m_presentFamily};
        float priority = 1.0F;
        std::vector<vk::DeviceQueueCreateInfo> queueInfos;
        queueInfos.reserve(uniqueFamilies.size());
        for (uint32_t familyId : uniqueFamilies) {
            queueInfos.push_back({{}, familyId, 1, &priority});
        }

        vk::DeviceCreateInfo createInfo{};
        createInfo.setQueueCreateInfos(queueInfos);
        createInfo.setPEnabledExtensionNames(kDeviceExtensions);

        m_device = m_physicalDevice.createDevice(createInfo);
        m_graphicsQueue = m_device.getQueue(m_graphicsFamily, 0);
        m_presentQueue = m_device.getQueue(m_presentFamily, 0);
    } catch (const vk::SystemError& err) {
        SONNET_LOG_ERROR("Failed to create logical device: {}", err.what());
        return false;
    }
    return true;
}

bool VulkanBackend::createSwapchain(sonnet::window::IWindow& window) {
    try {
        auto caps = m_physicalDevice.getSurfaceCapabilitiesKHR(m_surface);
        auto formats = m_physicalDevice.getSurfaceFormatsKHR(m_surface);
        auto presentModes = m_physicalDevice.getSurfacePresentModesKHR(m_surface);

        if (formats.empty()) {
            SONNET_LOG_ERROR("No surface formats available");
            return false;
        }

        vk::SurfaceFormatKHR chosenFormat = formats[0];
        for (const auto& fmt : formats) {
            if (fmt.format == vk::Format::eB8G8R8A8Srgb &&
                fmt.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
                chosenFormat = fmt;
                break;
            }
        }
        m_swapchainFormat = chosenFormat.format;

        vk::PresentModeKHR chosenMode = vk::PresentModeKHR::eFifo;
        for (const auto& mode : presentModes) {
            if (mode == vk::PresentModeKHR::eMailbox) {
                chosenMode = mode;
                break;
            }
        }

        if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            m_swapchainExtent = caps.currentExtent;
        } else {
            auto [w, h] = window.getSize();
            m_swapchainExtent.width = std::clamp(
                static_cast<uint32_t>(w), caps.minImageExtent.width, caps.maxImageExtent.width);
            m_swapchainExtent.height = std::clamp(
                static_cast<uint32_t>(h), caps.minImageExtent.height, caps.maxImageExtent.height);
        }

        uint32_t imageCount = std::min(
            caps.minImageCount + 1,
            caps.maxImageCount > 0 ? caps.maxImageCount : std::numeric_limits<uint32_t>::max());

        vk::SwapchainCreateInfoKHR createInfo{};
        createInfo.surface = m_surface;
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = chosenFormat.format;
        createInfo.imageColorSpace = chosenFormat.colorSpace;
        createInfo.imageExtent = m_swapchainExtent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
        createInfo.preTransform = caps.currentTransform;
        createInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
        createInfo.presentMode = chosenMode;
        createInfo.clipped = VK_TRUE;

        std::array<uint32_t, 2> familyIndices{m_graphicsFamily, m_presentFamily};
        if (m_graphicsFamily != m_presentFamily) {
            createInfo.imageSharingMode = vk::SharingMode::eConcurrent;
            createInfo.setQueueFamilyIndices(familyIndices);
        } else {
            createInfo.imageSharingMode = vk::SharingMode::eExclusive;
        }

        m_swapchain = m_device.createSwapchainKHR(createInfo);
        m_swapchainImages = m_device.getSwapchainImagesKHR(m_swapchain);

        m_swapchainImageViews.resize(m_swapchainImages.size());
        for (size_t i = 0; i < m_swapchainImages.size(); ++i) {
            vk::ImageViewCreateInfo viewInfo{};
            viewInfo.image = m_swapchainImages[i];
            viewInfo.viewType = vk::ImageViewType::e2D;
            viewInfo.format = m_swapchainFormat;
            viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.layerCount = 1;
            m_swapchainImageViews[i] = m_device.createImageView(viewInfo);
        }
    } catch (const vk::SystemError& err) {
        SONNET_LOG_ERROR("Failed to create swapchain: {}", err.what());
        return false;
    }
    return true;
}

// ─── Part 3: Render Pass + Pipeline ───────────────────────────────────────────

bool VulkanBackend::createRenderPass() {
    try {
        vk::AttachmentDescription colorAttachment{};
        colorAttachment.format = m_swapchainFormat;
        colorAttachment.samples = vk::SampleCountFlagBits::e1;
        colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
        colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
        colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
        colorAttachment.finalLayout = vk::ImageLayout::ePresentSrcKHR;

        vk::AttachmentReference colorRef{0, vk::ImageLayout::eColorAttachmentOptimal};

        vk::SubpassDescription subpass{};
        subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        subpass.setColorAttachments(colorRef);

        vk::SubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dep.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dep.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

        vk::RenderPassCreateInfo rpInfo{};
        rpInfo.setAttachments(colorAttachment);
        rpInfo.setSubpasses(subpass);
        rpInfo.setDependencies(dep);

        m_renderPass = m_device.createRenderPass(rpInfo);
    } catch (const vk::SystemError& err) {
        SONNET_LOG_ERROR("Failed to create render pass: {}", err.what());
        return false;
    }
    return true;
}

std::vector<char> VulkanBackend::loadSpirvFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        SONNET_LOG_ERROR("Failed to open SPIR-V file: {}", path);
        return {};
    }
    auto size = static_cast<size_t>(file.tellg());
    std::vector<char> buf(size);
    file.seekg(0);
    file.read(buf.data(), static_cast<std::streamsize>(size));
    return buf;
}

vk::ShaderModule VulkanBackend::createShaderModule(const std::vector<char>& code) {
    vk::ShaderModuleCreateInfo info{};
    info.codeSize = code.size();
    info.pCode = reinterpret_cast<const uint32_t*>(code.data());
    return m_device.createShaderModule(info);
}

bool VulkanBackend::createPipeline(const std::string& exeDir) {
    namespace fs = std::filesystem;

    auto vertCode = loadSpirvFile((fs::path(exeDir) / "shaders" / "triangle.vert.spv").string());
    auto fragCode = loadSpirvFile((fs::path(exeDir) / "shaders" / "triangle.frag.spv").string());
    if (vertCode.empty() || fragCode.empty()) {
        return false;
    }

    vk::ShaderModule vertModule;
    vk::ShaderModule fragModule;
    try {
        vertModule = createShaderModule(vertCode);
        fragModule = createShaderModule(fragCode);
    } catch (const vk::SystemError& err) {
        SONNET_LOG_ERROR("Failed to create shader modules: {}", err.what());
        return false;
    }

    vk::PipelineShaderStageCreateInfo vertStage{};
    vertStage.stage = vk::ShaderStageFlagBits::eVertex;
    vertStage.module = vertModule;
    vertStage.pName = "main";

    vk::PipelineShaderStageCreateInfo fragStage{};
    fragStage.stage = vk::ShaderStageFlagBits::eFragment;
    fragStage.module = fragModule;
    fragStage.pName = "main";

    std::array stages{vertStage, fragStage};

    vk::PipelineVertexInputStateCreateInfo vertexInput{};
    vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;

    vk::Viewport viewport{0,
                          0,
                          static_cast<float>(m_swapchainExtent.width),
                          static_cast<float>(m_swapchainExtent.height),
                          0.0F,
                          1.0F};
    vk::Rect2D scissor{{0, 0}, m_swapchainExtent};

    vk::PipelineViewportStateCreateInfo viewportState{};
    viewportState.setViewports(viewport);
    viewportState.setScissors(scissor);

    vk::PipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.polygonMode = vk::PolygonMode::eFill;
    rasterizer.lineWidth = 1.0F;
    rasterizer.cullMode = vk::CullModeFlagBits::eNone;
    rasterizer.frontFace = vk::FrontFace::eClockwise;

    vk::PipelineMultisampleStateCreateInfo multisampling{};
    multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

    vk::PipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask =
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

    vk::PipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.setAttachments(blendAttachment);

    bool pipelineOk = false;
    try {
        vk::PipelineLayoutCreateInfo layoutInfo{};
        m_pipelineLayout = m_device.createPipelineLayout(layoutInfo);

        vk::GraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.setStages(stages);
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.layout = m_pipelineLayout;
        pipelineInfo.renderPass = m_renderPass;
        pipelineInfo.subpass = 0;

        m_pipeline = m_device.createGraphicsPipeline(nullptr, pipelineInfo).value;
        pipelineOk = true;
    } catch (const vk::SystemError& err) {
        SONNET_LOG_ERROR("Failed to create graphics pipeline: {}", err.what());
    }

    m_device.destroyShaderModule(vertModule);
    m_device.destroyShaderModule(fragModule);
    return pipelineOk;
}

// ─── Part 4: Framebuffers, Command pool, Sync objects ─────────────────────────

bool VulkanBackend::createFramebuffers() {
    try {
        m_framebuffers.resize(m_swapchainImageViews.size());
        for (size_t i = 0; i < m_swapchainImageViews.size(); ++i) {
            vk::FramebufferCreateInfo fbInfo{};
            fbInfo.renderPass = m_renderPass;
            fbInfo.setAttachments(m_swapchainImageViews[i]);
            fbInfo.width = m_swapchainExtent.width;
            fbInfo.height = m_swapchainExtent.height;
            fbInfo.layers = 1;
            m_framebuffers[i] = m_device.createFramebuffer(fbInfo);
        }
    } catch (const vk::SystemError& err) {
        SONNET_LOG_ERROR("Failed to create framebuffers: {}", err.what());
        return false;
    }
    return true;
}

bool VulkanBackend::createCommandPool() {
    try {
        vk::CommandPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
        poolInfo.queueFamilyIndex = m_graphicsFamily;
        m_commandPool = m_device.createCommandPool(poolInfo);

        vk::CommandBufferAllocateInfo allocInfo{};
        allocInfo.commandPool = m_commandPool;
        allocInfo.level = vk::CommandBufferLevel::ePrimary;
        allocInfo.commandBufferCount = kMaxFramesInFlight;
        m_commandBuffers = m_device.allocateCommandBuffers(allocInfo);
    } catch (const vk::SystemError& err) {
        SONNET_LOG_ERROR("Failed to create command pool: {}", err.what());
        return false;
    }
    return true;
}

bool VulkanBackend::createSyncObjects() {
    try {
        m_imageAvailableSemaphores.resize(kMaxFramesInFlight);
        m_renderFinishedSemaphores.resize(kMaxFramesInFlight);
        m_inFlightFences.resize(kMaxFramesInFlight);

        vk::SemaphoreCreateInfo semInfo{};
        vk::FenceCreateInfo fenceInfo{vk::FenceCreateFlagBits::eSignaled};

        for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
            m_imageAvailableSemaphores[i] = m_device.createSemaphore(semInfo);
            m_renderFinishedSemaphores[i] = m_device.createSemaphore(semInfo);
            m_inFlightFences[i] = m_device.createFence(fenceInfo);
        }
    } catch (const vk::SystemError& err) {
        SONNET_LOG_ERROR("Failed to create sync objects: {}", err.what());
        return false;
    }
    return true;
}

// ─── Part 5: Render loop + Swapchain rebuild ──────────────────────────────────

void VulkanBackend::destroySwapchainResources() {
    for (auto& framebuf : m_framebuffers) {
        m_device.destroyFramebuffer(framebuf);
    }
    m_framebuffers.clear();
    for (auto& imgView : m_swapchainImageViews) {
        m_device.destroyImageView(imgView);
    }
    m_swapchainImageViews.clear();
    m_swapchainImages.clear();
}

bool VulkanBackend::rebuildSwapchain() {
    m_device.waitIdle();
    destroySwapchainResources();
    if (m_swapchain) {
        m_device.destroySwapchainKHR(m_swapchain);
        m_swapchain = nullptr;
    }
    return createSwapchain(*m_window) && createFramebuffers();
}

void VulkanBackend::beginFrame() {
    try {
        static_cast<void>(m_device.waitForFences(m_inFlightFences[m_currentFrame], VK_TRUE,
                                                 std::numeric_limits<uint64_t>::max()));

        auto result =
            m_device.acquireNextImageKHR(m_swapchain, std::numeric_limits<uint64_t>::max(),
                                         m_imageAvailableSemaphores[m_currentFrame], nullptr);

        if (result.result == vk::Result::eErrorOutOfDateKHR ||
            result.result == vk::Result::eSuboptimalKHR) {
            rebuildSwapchain();
            m_frameInProgress = false;
            return;
        }

        m_currentImageIndex = result.value;
        m_device.resetFences(m_inFlightFences[m_currentFrame]);

        auto& cmd = m_commandBuffers[m_currentFrame];
        cmd.reset();
        cmd.begin(vk::CommandBufferBeginInfo{});
        m_frameInProgress = true;
    } catch (const vk::SystemError& err) {
        SONNET_LOG_ERROR("beginFrame error: {}", err.what());
        m_frameInProgress = false;
    }
}

void VulkanBackend::drawPrimitive() {
    if (!m_frameInProgress) {
        return;
    }

    auto& cmd = m_commandBuffers[m_currentFrame];
    vk::ClearValue clearColor{vk::ClearColorValue{std::array<float, 4>{
        kClearColorComponent, kClearColorComponent, kClearColorComponent, 1.0F}}};

    vk::RenderPassBeginInfo rpBegin{};
    rpBegin.renderPass = m_renderPass;
    rpBegin.framebuffer = m_framebuffers[m_currentImageIndex];
    rpBegin.renderArea.extent = m_swapchainExtent;
    rpBegin.setClearValues(clearColor);

    cmd.beginRenderPass(rpBegin, vk::SubpassContents::eInline);
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_pipeline);
    cmd.draw(3, 1, 0, 0);
    cmd.endRenderPass();
}

void VulkanBackend::endFrame() {
    if (!m_frameInProgress) {
        return;
    }
    m_frameInProgress = false;

    try {
        auto& cmd = m_commandBuffers[m_currentFrame];
        cmd.end();

        vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        vk::SubmitInfo submitInfo{};
        submitInfo.setWaitSemaphores(m_imageAvailableSemaphores[m_currentFrame]);
        submitInfo.setWaitDstStageMask(waitStage);
        submitInfo.setCommandBuffers(cmd);
        submitInfo.setSignalSemaphores(m_renderFinishedSemaphores[m_currentFrame]);

        m_graphicsQueue.submit(submitInfo, m_inFlightFences[m_currentFrame]);

        vk::PresentInfoKHR presentInfo{};
        presentInfo.setWaitSemaphores(m_renderFinishedSemaphores[m_currentFrame]);
        presentInfo.setSwapchains(m_swapchain);
        presentInfo.setImageIndices(m_currentImageIndex);

        auto result = m_presentQueue.presentKHR(presentInfo);
        if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR) {
            rebuildSwapchain();
        }
    } catch (const vk::SystemError& err) {
        SONNET_LOG_ERROR("endFrame error: {}", err.what());
    }

    m_currentFrame = (m_currentFrame + 1) % kMaxFramesInFlight;
}

// ─── Part 6: Offscreen Resources + Native Handles ────────────────────────────

uint32_t VulkanBackend::findMemoryType(uint32_t typeFilter,
                                       vk::MemoryPropertyFlags properties) const {
    auto memProps = m_physicalDevice.getMemoryProperties();
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if (((typeFilter & (1U << i)) != 0U) &&
            (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    SONNET_LOG_ERROR("Failed to find suitable Vulkan memory type");
    return 0;
}

vk::CommandBuffer VulkanBackend::beginOneTimeCommands() const {
    vk::CommandBufferAllocateInfo allocInfo{};
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandPool = m_commandPool;
    allocInfo.commandBufferCount = 1;
    auto cmd = m_device.allocateCommandBuffers(allocInfo)[0];
    cmd.begin(vk::CommandBufferBeginInfo{vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
    return cmd;
}

void VulkanBackend::endOneTimeCommands(vk::CommandBuffer cmd) const {
    cmd.end();
    vk::SubmitInfo submitInfo{};
    submitInfo.setCommandBuffers(cmd);
    m_graphicsQueue.submit(submitInfo);
    m_graphicsQueue.waitIdle();
    m_device.freeCommandBuffers(m_commandPool, cmd);
}

bool VulkanBackend::createImguiDescriptorPool() {
    vk::DescriptorPoolSize poolSize{};
    poolSize.type = vk::DescriptorType::eCombinedImageSampler;
    constexpr uint32_t kImguiDescriptorCount = 8; // font atlas + viewport + headroom
    poolSize.descriptorCount = kImguiDescriptorCount;

    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolInfo.maxSets = kImguiDescriptorCount;
    poolInfo.setPoolSizes(poolSize);

    try {
        m_imguiDescriptorPool = m_device.createDescriptorPool(poolInfo);
    } catch (const vk::SystemError& err) {
        SONNET_LOG_ERROR("Failed to create ImGui descriptor pool: {}", err.what());
        return false;
    }
    return true;
}

bool VulkanBackend::createOffscreenResources() {
    m_offscreenWidth = m_swapchainExtent.width;
    m_offscreenHeight = m_swapchainExtent.height;

    try {
        // Image
        vk::ImageCreateInfo imageInfo{};
        imageInfo.imageType = vk::ImageType::e2D;
        imageInfo.format = m_swapchainFormat;
        imageInfo.extent = vk::Extent3D{m_offscreenWidth, m_offscreenHeight, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = vk::SampleCountFlagBits::e1;
        imageInfo.tiling = vk::ImageTiling::eOptimal;
        imageInfo.usage =
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled;
        imageInfo.initialLayout = vk::ImageLayout::eUndefined;
        m_offscreenImage = m_device.createImage(imageInfo);

        auto memReqs = m_device.getImageMemoryRequirements(m_offscreenImage);
        vk::MemoryAllocateInfo allocInfo{};
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex =
            findMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
        m_offscreenMemory = m_device.allocateMemory(allocInfo);
        m_device.bindImageMemory(m_offscreenImage, m_offscreenMemory, 0);

        // Image view
        vk::ImageViewCreateInfo viewInfo{};
        viewInfo.image = m_offscreenImage;
        viewInfo.viewType = vk::ImageViewType::e2D;
        viewInfo.format = m_swapchainFormat;
        viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        m_offscreenImageView = m_device.createImageView(viewInfo);

        // Sampler
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.maxLod = 0.0F;
        m_offscreenSampler = m_device.createSampler(samplerInfo);

        // Transition image from UNDEFINED to SHADER_READ_ONLY_OPTIMAL before first use
        auto cmd = beginOneTimeCommands();
        vk::ImageMemoryBarrier barrier{};
        barrier.oldLayout = vk::ImageLayout::eUndefined;
        barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = m_offscreenImage;
        barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = {};
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                            vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, barrier);
        endOneTimeCommands(cmd);

        // Offscreen render pass
        vk::AttachmentDescription colorAtt{};
        colorAtt.format = m_swapchainFormat;
        colorAtt.samples = vk::SampleCountFlagBits::e1;
        colorAtt.loadOp = vk::AttachmentLoadOp::eClear;
        colorAtt.storeOp = vk::AttachmentStoreOp::eStore;
        colorAtt.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        colorAtt.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        colorAtt.initialLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        colorAtt.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::AttachmentReference colorRef{0, vk::ImageLayout::eColorAttachmentOptimal};
        vk::SubpassDescription subpass{};
        subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        subpass.setColorAttachments(colorRef);

        std::array<vk::SubpassDependency, 2> deps{};
        deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass = 0;
        deps[0].srcStageMask = vk::PipelineStageFlagBits::eFragmentShader;
        deps[0].dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        deps[0].srcAccessMask = vk::AccessFlagBits::eShaderRead;
        deps[0].dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
        deps[0].dependencyFlags = vk::DependencyFlagBits::eByRegion;
        deps[1].srcSubpass = 0;
        deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        deps[1].dstStageMask = vk::PipelineStageFlagBits::eFragmentShader;
        deps[1].srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
        deps[1].dstAccessMask = vk::AccessFlagBits::eShaderRead;
        deps[1].dependencyFlags = vk::DependencyFlagBits::eByRegion;

        vk::RenderPassCreateInfo rpInfo{};
        rpInfo.setAttachments(colorAtt);
        rpInfo.setSubpasses(subpass);
        rpInfo.setDependencies(deps);
        m_offscreenRenderPass = m_device.createRenderPass(rpInfo);

        // Offscreen framebuffer
        vk::FramebufferCreateInfo fbInfo{};
        fbInfo.renderPass = m_offscreenRenderPass;
        fbInfo.setAttachments(m_offscreenImageView);
        fbInfo.width = m_offscreenWidth;
        fbInfo.height = m_offscreenHeight;
        fbInfo.layers = 1;
        m_offscreenFramebuffer = m_device.createFramebuffer(fbInfo);

        // Viewport descriptor set layout: binding 0 = combined image sampler (matches ImGui)
        vk::DescriptorSetLayoutBinding dslBinding{};
        dslBinding.binding = 0;
        dslBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        dslBinding.descriptorCount = 1;
        dslBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;

        vk::DescriptorSetLayoutCreateInfo dslInfo{};
        dslInfo.setBindings(dslBinding);
        m_viewportDescriptorSetLayout = m_device.createDescriptorSetLayout(dslInfo);

        // Allocate viewport descriptor set from the ImGui pool
        vk::DescriptorSetAllocateInfo dsAllocInfo{};
        dsAllocInfo.descriptorPool = m_imguiDescriptorPool;
        dsAllocInfo.setSetLayouts(m_viewportDescriptorSetLayout);
        m_offscreenDescriptorSet = m_device.allocateDescriptorSets(dsAllocInfo)[0];

        // Update with offscreen image + sampler
        vk::DescriptorImageInfo imgInfo{};
        imgInfo.sampler = m_offscreenSampler;
        imgInfo.imageView = m_offscreenImageView;
        imgInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::WriteDescriptorSet write{};
        write.dstSet = m_offscreenDescriptorSet;
        write.dstBinding = 0;
        write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        write.setImageInfo(imgInfo);
        m_device.updateDescriptorSets(write, {});

    } catch (const vk::SystemError& err) {
        SONNET_LOG_ERROR("Failed to create offscreen resources: {}", err.what());
        return false;
    }
    return true;
}

void VulkanBackend::destroyOffscreenResources() {
    if (m_offscreenDescriptorSet && m_imguiDescriptorPool) {
        m_device.freeDescriptorSets(m_imguiDescriptorPool, m_offscreenDescriptorSet);
        m_offscreenDescriptorSet = nullptr;
    }
    if (m_viewportDescriptorSetLayout) {
        m_device.destroyDescriptorSetLayout(m_viewportDescriptorSetLayout);
        m_viewportDescriptorSetLayout = nullptr;
    }
    if (m_offscreenFramebuffer) {
        m_device.destroyFramebuffer(m_offscreenFramebuffer);
        m_offscreenFramebuffer = nullptr;
    }
    if (m_offscreenRenderPass) {
        m_device.destroyRenderPass(m_offscreenRenderPass);
        m_offscreenRenderPass = nullptr;
    }
    if (m_offscreenSampler) {
        m_device.destroySampler(m_offscreenSampler);
        m_offscreenSampler = nullptr;
    }
    if (m_offscreenImageView) {
        m_device.destroyImageView(m_offscreenImageView);
        m_offscreenImageView = nullptr;
    }
    if (m_offscreenImage) {
        m_device.destroyImage(m_offscreenImage);
        m_offscreenImage = nullptr;
    }
    if (m_offscreenMemory) {
        m_device.freeMemory(m_offscreenMemory);
        m_offscreenMemory = nullptr;
    }
}

RendererNativeHandles VulkanBackend::getNativeHandles() const {
    RendererNativeHandles handles{};
    handles.instance = reinterpret_cast<uint64_t>(static_cast<VkInstance>(m_instance));
    handles.physicalDevice =
        reinterpret_cast<uint64_t>(static_cast<VkPhysicalDevice>(m_physicalDevice));
    handles.device = reinterpret_cast<uint64_t>(static_cast<VkDevice>(m_device));
    handles.graphicsQueue = reinterpret_cast<uint64_t>(static_cast<VkQueue>(m_graphicsQueue));
    handles.renderPass = reinterpret_cast<uint64_t>(static_cast<VkRenderPass>(m_renderPass));
    handles.descriptorPool =
        reinterpret_cast<uint64_t>(static_cast<VkDescriptorPool>(m_imguiDescriptorPool));
    handles.graphicsQueueFamily = m_graphicsFamily;
    handles.imageCount = static_cast<uint32_t>(m_swapchainImages.size());
    return handles;
}

void VulkanBackend::renderSceneOffscreen() {
    if (!m_initialized) {
        return;
    }
    // Begin the frame if not already in progress
    if (!m_frameInProgress) {
        beginFrame();
    }
    if (!m_frameInProgress) {
        return;
    }

    auto& cmd = m_commandBuffers[m_currentFrame];
    vk::ClearValue clearColor{vk::ClearColorValue{std::array<float, 4>{
        kClearColorComponent, kClearColorComponent, kClearColorComponent, 1.0F}}};

    vk::RenderPassBeginInfo rpBegin{};
    rpBegin.renderPass = m_offscreenRenderPass;
    rpBegin.framebuffer = m_offscreenFramebuffer;
    rpBegin.renderArea.extent = vk::Extent2D{m_offscreenWidth, m_offscreenHeight};
    rpBegin.setClearValues(clearColor);

    cmd.beginRenderPass(rpBegin, vk::SubpassContents::eInline);
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_pipeline);
    cmd.draw(3, 1, 0, 0);
    cmd.endRenderPass();
}

uint64_t VulkanBackend::getViewportTextureId() const {
    return reinterpret_cast<uint64_t>(static_cast<VkDescriptorSet>(m_offscreenDescriptorSet));
}

void VulkanBackend::beginEditorRenderPass() {
    if (!m_frameInProgress) {
        beginFrame();
    }
    if (!m_frameInProgress) {
        return;
    }
    auto& cmd = m_commandBuffers[m_currentFrame];
    vk::ClearValue clearColor{vk::ClearColorValue{std::array<float, 4>{0.0F, 0.0F, 0.0F, 1.0F}}};

    vk::RenderPassBeginInfo rpBegin{};
    rpBegin.renderPass = m_renderPass;
    rpBegin.framebuffer = m_framebuffers[m_currentImageIndex];
    rpBegin.renderArea.extent = m_swapchainExtent;
    rpBegin.setClearValues(clearColor);

    cmd.beginRenderPass(rpBegin, vk::SubpassContents::eInline);
}

void VulkanBackend::endEditorRenderPass() {
    if (!m_frameInProgress) {
        return;
    }
    m_commandBuffers[m_currentFrame].endRenderPass();
    endFrame();
}

uint64_t VulkanBackend::getCurrentCommandBuffer() const {
    if (!m_frameInProgress) {
        return 0;
    }
    return reinterpret_cast<uint64_t>(
        static_cast<VkCommandBuffer>(m_commandBuffers[m_currentFrame]));
}

glm::ivec2 VulkanBackend::getOffscreenSize() const {
    return {static_cast<int32_t>(m_offscreenWidth), static_cast<int32_t>(m_offscreenHeight)};
}

// ─── Part 7: Forward lit pipeline ─────────────────────────────────────────────

namespace {

struct CameraUboData {
    glm::mat4 view{1.0F};
    glm::mat4 proj{1.0F};
    glm::vec3 cameraPos{0.0F, 0.0F, 0.0F};
    float     _pad{0.0F};
};

struct LightsUboData {
    glm::vec3 direction{0.0F, -1.0F, 0.0F};
    float     intensity{1.0F};
    glm::vec3 color{1.0F, 1.0F, 1.0F};
    float     _pad{0.0F};
    int32_t   hasLight{0};
    int32_t   _pad2[3]{};
};

struct ForwardPushConstants {
    glm::mat4 model{1.0F};
    uint32_t  objectId{0};
    float     _pad[3]{};
};

} // namespace

vk::Buffer VulkanBackend::createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage,
                                        vk::MemoryPropertyFlags props,
                                        vk::DeviceMemory& outMem) {
    vk::BufferCreateInfo bufInfo{};
    bufInfo.size = size;
    bufInfo.usage = usage;
    bufInfo.sharingMode = vk::SharingMode::eExclusive;
    auto buf = m_device.createBuffer(bufInfo);

    auto memReqs = m_device.getBufferMemoryRequirements(buf);
    vk::MemoryAllocateInfo allocInfo{};
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, props);
    outMem = m_device.allocateMemory(allocInfo);
    m_device.bindBufferMemory(buf, outMem, 0);
    return buf;
}

bool VulkanBackend::createForwardPipeline() {
    namespace fs = std::filesystem;

    try {
        // Depth image (D32Sfloat)
        vk::ImageCreateInfo depthInfo{};
        depthInfo.imageType = vk::ImageType::e2D;
        depthInfo.format = vk::Format::eD32Sfloat;
        depthInfo.extent = vk::Extent3D{m_offscreenWidth, m_offscreenHeight, 1};
        depthInfo.mipLevels = 1;
        depthInfo.arrayLayers = 1;
        depthInfo.samples = vk::SampleCountFlagBits::e1;
        depthInfo.tiling = vk::ImageTiling::eOptimal;
        depthInfo.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment;
        depthInfo.initialLayout = vk::ImageLayout::eUndefined;
        m_depthImage = m_device.createImage(depthInfo);

        auto depthMemReqs = m_device.getImageMemoryRequirements(m_depthImage);
        vk::MemoryAllocateInfo depthAllocInfo{};
        depthAllocInfo.allocationSize = depthMemReqs.size;
        depthAllocInfo.memoryTypeIndex =
            findMemoryType(depthMemReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
        m_depthMemory = m_device.allocateMemory(depthAllocInfo);
        m_device.bindImageMemory(m_depthImage, m_depthMemory, 0);

        vk::ImageViewCreateInfo depthViewInfo{};
        depthViewInfo.image = m_depthImage;
        depthViewInfo.viewType = vk::ImageViewType::e2D;
        depthViewInfo.format = vk::Format::eD32Sfloat;
        depthViewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
        depthViewInfo.subresourceRange.levelCount = 1;
        depthViewInfo.subresourceRange.layerCount = 1;
        m_depthImageView = m_device.createImageView(depthViewInfo);

        // Forward render pass: color (SR→SR) + depth
        std::array<vk::AttachmentDescription, 2> attDescs{};
        attDescs[0].format = m_swapchainFormat;
        attDescs[0].samples = vk::SampleCountFlagBits::e1;
        attDescs[0].loadOp = vk::AttachmentLoadOp::eClear;
        attDescs[0].storeOp = vk::AttachmentStoreOp::eStore;
        attDescs[0].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        attDescs[0].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        attDescs[0].initialLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        attDescs[0].finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        attDescs[1].format = vk::Format::eD32Sfloat;
        attDescs[1].samples = vk::SampleCountFlagBits::e1;
        attDescs[1].loadOp = vk::AttachmentLoadOp::eClear;
        attDescs[1].storeOp = vk::AttachmentStoreOp::eDontCare;
        attDescs[1].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        attDescs[1].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        attDescs[1].initialLayout = vk::ImageLayout::eUndefined;
        attDescs[1].finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

        vk::AttachmentReference colorRef{0, vk::ImageLayout::eColorAttachmentOptimal};
        vk::AttachmentReference depthRef{1, vk::ImageLayout::eDepthStencilAttachmentOptimal};

        vk::SubpassDescription subpass{};
        subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        subpass.setColorAttachments(colorRef);
        subpass.pDepthStencilAttachment = &depthRef;

        std::array<vk::SubpassDependency, 2> deps{};
        deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass = 0;
        deps[0].srcStageMask = vk::PipelineStageFlagBits::eFragmentShader;
        deps[0].dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput |
                               vk::PipelineStageFlagBits::eEarlyFragmentTests;
        deps[0].srcAccessMask = vk::AccessFlagBits::eShaderRead;
        deps[0].dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite |
                                vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        deps[0].dependencyFlags = vk::DependencyFlagBits::eByRegion;

        deps[1].srcSubpass = 0;
        deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput |
                               vk::PipelineStageFlagBits::eLateFragmentTests;
        deps[1].dstStageMask = vk::PipelineStageFlagBits::eFragmentShader;
        deps[1].srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite |
                                vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        deps[1].dstAccessMask = vk::AccessFlagBits::eShaderRead;
        deps[1].dependencyFlags = vk::DependencyFlagBits::eByRegion;

        vk::RenderPassCreateInfo rpInfo{};
        rpInfo.setAttachments(attDescs);
        rpInfo.setSubpasses(subpass);
        rpInfo.setDependencies(deps);
        m_forwardRenderPass = m_device.createRenderPass(rpInfo);

        // Forward framebuffer (shared offscreen color image + depth)
        std::array<vk::ImageView, 2> fbAtts{m_offscreenImageView, m_depthImageView};
        vk::FramebufferCreateInfo fbInfo{};
        fbInfo.renderPass = m_forwardRenderPass;
        fbInfo.setAttachments(fbAtts);
        fbInfo.width = m_offscreenWidth;
        fbInfo.height = m_offscreenHeight;
        fbInfo.layers = 1;
        m_forwardFramebuffer = m_device.createFramebuffer(fbInfo);

        // UBO buffers (one per frame in flight, persistently mapped)
        m_cameraUboBuffers.resize(kMaxFramesInFlight);
        m_cameraUboMemories.resize(kMaxFramesInFlight);
        m_cameraUboMapped.resize(kMaxFramesInFlight);
        m_lightsUboBuffers.resize(kMaxFramesInFlight);
        m_lightsUboMemories.resize(kMaxFramesInFlight);
        m_lightsUboMapped.resize(kMaxFramesInFlight);

        constexpr auto kHostProps =
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
            m_cameraUboBuffers[i] =
                createBuffer(sizeof(CameraUboData), vk::BufferUsageFlagBits::eUniformBuffer,
                             kHostProps, m_cameraUboMemories[i]);
            m_cameraUboMapped[i] = m_device.mapMemory(m_cameraUboMemories[i], 0, sizeof(CameraUboData));

            m_lightsUboBuffers[i] =
                createBuffer(sizeof(LightsUboData), vk::BufferUsageFlagBits::eUniformBuffer,
                             kHostProps, m_lightsUboMemories[i]);
            m_lightsUboMapped[i] = m_device.mapMemory(m_lightsUboMemories[i], 0, sizeof(LightsUboData));
        }

        // Descriptor set layout: binding 0 = CameraUBO, binding 1 = LightsUBO
        std::array<vk::DescriptorSetLayoutBinding, 2> dslBindings{};
        dslBindings[0].binding = 0;
        dslBindings[0].descriptorType = vk::DescriptorType::eUniformBuffer;
        dslBindings[0].descriptorCount = 1;
        dslBindings[0].stageFlags =
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

        dslBindings[1].binding = 1;
        dslBindings[1].descriptorType = vk::DescriptorType::eUniformBuffer;
        dslBindings[1].descriptorCount = 1;
        dslBindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;

        vk::DescriptorSetLayoutCreateInfo dslInfo{};
        dslInfo.setBindings(dslBindings);
        m_forwardDescriptorSetLayout = m_device.createDescriptorSetLayout(dslInfo);

        // Descriptor pool
        vk::DescriptorPoolSize poolSize{};
        poolSize.type = vk::DescriptorType::eUniformBuffer;
        poolSize.descriptorCount = kMaxFramesInFlight * 2; // 2 UBOs per frame
        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = kMaxFramesInFlight;
        poolInfo.setPoolSizes(poolSize);
        m_forwardDescriptorPool = m_device.createDescriptorPool(poolInfo);

        // Allocate descriptor sets (one per frame in flight)
        std::vector<vk::DescriptorSetLayout> layouts(kMaxFramesInFlight,
                                                     m_forwardDescriptorSetLayout);
        vk::DescriptorSetAllocateInfo dsAllocInfo{};
        dsAllocInfo.descriptorPool = m_forwardDescriptorPool;
        dsAllocInfo.setSetLayouts(layouts);
        m_forwardDescriptorSets = m_device.allocateDescriptorSets(dsAllocInfo);

        // Update descriptor sets
        for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
            vk::DescriptorBufferInfo camBufInfo{};
            camBufInfo.buffer = m_cameraUboBuffers[i];
            camBufInfo.range = sizeof(CameraUboData);

            vk::DescriptorBufferInfo lightBufInfo{};
            lightBufInfo.buffer = m_lightsUboBuffers[i];
            lightBufInfo.range = sizeof(LightsUboData);

            std::array<vk::WriteDescriptorSet, 2> writes{};
            writes[0].dstSet = m_forwardDescriptorSets[i];
            writes[0].dstBinding = 0;
            writes[0].descriptorType = vk::DescriptorType::eUniformBuffer;
            writes[0].setBufferInfo(camBufInfo);

            writes[1].dstSet = m_forwardDescriptorSets[i];
            writes[1].dstBinding = 1;
            writes[1].descriptorType = vk::DescriptorType::eUniformBuffer;
            writes[1].setBufferInfo(lightBufInfo);

            m_device.updateDescriptorSets(writes, {});
        }

        // Pipeline layout: descriptor set + push constants
        vk::PushConstantRange pcRange{};
        pcRange.stageFlags = vk::ShaderStageFlagBits::eVertex;
        pcRange.offset = 0;
        pcRange.size = sizeof(ForwardPushConstants);

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setSetLayouts(m_forwardDescriptorSetLayout);
        layoutInfo.setPushConstantRanges(pcRange);
        m_forwardPipelineLayout = m_device.createPipelineLayout(layoutInfo);

        // Shader modules
        auto vertCode =
            loadSpirvFile((fs::path(m_exeDir) / "shaders" / "forward_lit.vert.spv").string());
        auto fragCode =
            loadSpirvFile((fs::path(m_exeDir) / "shaders" / "forward_lit.frag.spv").string());
        if (vertCode.empty() || fragCode.empty()) {
            return false;
        }

        auto vertMod = createShaderModule(vertCode);
        auto fragMod = createShaderModule(fragCode);

        std::array<vk::PipelineShaderStageCreateInfo, 2> stages{};
        stages[0].stage = vk::ShaderStageFlagBits::eVertex;
        stages[0].module = vertMod;
        stages[0].pName = "main";
        stages[1].stage = vk::ShaderStageFlagBits::eFragment;
        stages[1].module = fragMod;
        stages[1].pName = "main";

        // Vertex input: position (vec3), texCoord (vec2), normal (vec3) -> stride 32
        vk::VertexInputBindingDescription bindingDesc{};
        bindingDesc.binding = 0;
        bindingDesc.stride = sizeof(Vertex);
        bindingDesc.inputRate = vk::VertexInputRate::eVertex;

        std::array<vk::VertexInputAttributeDescription, 3> attrDescs{};
        attrDescs[0] = {0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, position)};
        attrDescs[1] = {1, 0, vk::Format::eR32G32Sfloat,    offsetof(Vertex, texCoord)};
        attrDescs[2] = {2, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, normal)};

        vk::PipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.setVertexBindingDescriptions(bindingDesc);
        vertexInput.setVertexAttributeDescriptions(attrDescs);

        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;

        vk::PipelineViewportStateCreateInfo viewportState{};
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        vk::PipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.polygonMode = vk::PolygonMode::eFill;
        rasterizer.lineWidth = 1.0F;
        rasterizer.cullMode = vk::CullModeFlagBits::eBack;
        rasterizer.frontFace = vk::FrontFace::eCounterClockwise;

        vk::PipelineMultisampleStateCreateInfo multisampling{};
        multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

        vk::PipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = vk::CompareOp::eLess;

        vk::PipelineColorBlendAttachmentState blendAttachment{};
        blendAttachment.colorWriteMask =
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
            vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

        vk::PipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.setAttachments(blendAttachment);

        std::array dynStates{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        vk::PipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.setDynamicStates(dynStates);

        vk::GraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.setStages(stages);
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = m_forwardPipelineLayout;
        pipelineInfo.renderPass = m_forwardRenderPass;
        pipelineInfo.subpass = 0;

        m_forwardPipeline = m_device.createGraphicsPipeline(nullptr, pipelineInfo).value;

        m_device.destroyShaderModule(fragMod);
        m_device.destroyShaderModule(vertMod);

    } catch (const vk::SystemError& err) {
        SONNET_LOG_ERROR("Failed to create forward pipeline: {}", err.what());
        return false;
    }
    return true;
}

void VulkanBackend::destroyForwardResources() {
    for (uint32_t i = 0; i < m_cameraUboBuffers.size(); ++i) {
        if (m_cameraUboMemories[i]) {
            m_device.unmapMemory(m_cameraUboMemories[i]);
            m_device.freeMemory(m_cameraUboMemories[i]);
            m_cameraUboMemories[i] = nullptr;
        }
        if (m_cameraUboBuffers[i]) {
            m_device.destroyBuffer(m_cameraUboBuffers[i]);
            m_cameraUboBuffers[i] = nullptr;
        }
    }
    m_cameraUboBuffers.clear();
    m_cameraUboMemories.clear();
    m_cameraUboMapped.clear();

    for (uint32_t i = 0; i < m_lightsUboBuffers.size(); ++i) {
        if (m_lightsUboMemories[i]) {
            m_device.unmapMemory(m_lightsUboMemories[i]);
            m_device.freeMemory(m_lightsUboMemories[i]);
            m_lightsUboMemories[i] = nullptr;
        }
        if (m_lightsUboBuffers[i]) {
            m_device.destroyBuffer(m_lightsUboBuffers[i]);
            m_lightsUboBuffers[i] = nullptr;
        }
    }
    m_lightsUboBuffers.clear();
    m_lightsUboMemories.clear();
    m_lightsUboMapped.clear();

    if (m_forwardDescriptorPool) {
        m_device.destroyDescriptorPool(m_forwardDescriptorPool);
        m_forwardDescriptorPool = nullptr;
    }
    if (m_forwardDescriptorSetLayout) {
        m_device.destroyDescriptorSetLayout(m_forwardDescriptorSetLayout);
        m_forwardDescriptorSetLayout = nullptr;
    }
    if (m_forwardPipeline) {
        m_device.destroyPipeline(m_forwardPipeline);
        m_forwardPipeline = nullptr;
    }
    if (m_forwardPipelineLayout) {
        m_device.destroyPipelineLayout(m_forwardPipelineLayout);
        m_forwardPipelineLayout = nullptr;
    }
    if (m_forwardFramebuffer) {
        m_device.destroyFramebuffer(m_forwardFramebuffer);
        m_forwardFramebuffer = nullptr;
    }
    if (m_forwardRenderPass) {
        m_device.destroyRenderPass(m_forwardRenderPass);
        m_forwardRenderPass = nullptr;
    }
    if (m_depthImageView) {
        m_device.destroyImageView(m_depthImageView);
        m_depthImageView = nullptr;
    }
    if (m_depthImage) {
        m_device.destroyImage(m_depthImage);
        m_depthImage = nullptr;
    }
    if (m_depthMemory) {
        m_device.freeMemory(m_depthMemory);
        m_depthMemory = nullptr;
    }

    // Release any remaining GPU meshes
    for (auto& [handle, gpuMesh] : m_meshes) {
        m_device.destroyBuffer(gpuMesh.indexBuffer);
        m_device.freeMemory(gpuMesh.indexMemory);
        m_device.destroyBuffer(gpuMesh.vertexBuffer);
        m_device.freeMemory(gpuMesh.vertexMemory);
    }
    m_meshes.clear();
}

uint64_t VulkanBackend::uploadMesh(const CPUMesh& mesh) {
    if (!m_initialized || mesh.vertices.empty() || mesh.indices.empty()) {
        return 0;
    }

    try {
        GpuMesh gpuMesh{};
        gpuMesh.indexCount = static_cast<uint32_t>(mesh.indices.size());

        const vk::DeviceSize vertSize = sizeof(Vertex) * mesh.vertices.size();
        const vk::DeviceSize idxSize = sizeof(uint32_t) * mesh.indices.size();

        // Vertex buffer
        vk::DeviceMemory stagingMem;
        auto staging = createBuffer(
            vertSize, vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
            stagingMem);
        auto* mapped = m_device.mapMemory(stagingMem, 0, vertSize);
        std::memcpy(mapped, mesh.vertices.data(), static_cast<size_t>(vertSize));
        m_device.unmapMemory(stagingMem);

        gpuMesh.vertexBuffer = createBuffer(
            vertSize,
            vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal, gpuMesh.vertexMemory);

        auto cmd = beginOneTimeCommands();
        vk::BufferCopy copyRegion{0, 0, vertSize};
        cmd.copyBuffer(staging, gpuMesh.vertexBuffer, copyRegion);
        endOneTimeCommands(cmd);

        m_device.destroyBuffer(staging);
        m_device.freeMemory(stagingMem);

        // Index buffer
        vk::DeviceMemory idxStagingMem;
        auto idxStaging = createBuffer(
            idxSize, vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
            idxStagingMem);
        auto* idxMapped = m_device.mapMemory(idxStagingMem, 0, idxSize);
        std::memcpy(idxMapped, mesh.indices.data(), static_cast<size_t>(idxSize));
        m_device.unmapMemory(idxStagingMem);

        gpuMesh.indexBuffer = createBuffer(
            idxSize,
            vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal, gpuMesh.indexMemory);

        auto idxCmd = beginOneTimeCommands();
        vk::BufferCopy idxCopy{0, 0, idxSize};
        idxCmd.copyBuffer(idxStaging, gpuMesh.indexBuffer, idxCopy);
        endOneTimeCommands(idxCmd);

        m_device.destroyBuffer(idxStaging);
        m_device.freeMemory(idxStagingMem);

        const uint64_t handle = m_nextMeshHandle++;
        m_meshes[handle] = gpuMesh;
        return handle;

    } catch (const vk::SystemError& err) {
        SONNET_LOG_ERROR("uploadMesh failed: {}", err.what());
        return 0;
    }
}

void VulkanBackend::releaseMesh(uint64_t handle) {
    if (m_meshes.find(handle) == m_meshes.end()) {
        return;
    }
    // Queue for destruction at the start of the next renderScene(), after the current
    // frame's command buffer has been submitted and the GPU is done with the buffers.
    m_pendingRelease.push_back(handle);
    // Remove from m_lastDesc immediately so pick() doesn't reference the handle.
    auto& objs = m_lastDesc.objects;
    objs.erase(std::remove_if(objs.begin(), objs.end(),
                              [handle](const DrawItem& d) { return d.meshHandle == handle; }),
               objs.end());
}

void VulkanBackend::flushPendingReleases() {
    if (m_pendingRelease.empty()) {
        return;
    }
    m_device.waitIdle();
    for (uint64_t handle : m_pendingRelease) {
        auto it = m_meshes.find(handle);
        if (it == m_meshes.end()) {
            continue;
        }
        auto& gpuMesh = it->second;
        m_device.destroyBuffer(gpuMesh.indexBuffer);
        m_device.freeMemory(gpuMesh.indexMemory);
        m_device.destroyBuffer(gpuMesh.vertexBuffer);
        m_device.freeMemory(gpuMesh.vertexMemory);
        m_meshes.erase(it);
    }
    m_pendingRelease.clear();
}

void VulkanBackend::renderScene(const SceneRenderDesc& desc) {
    if (!m_initialized || !m_forwardPipeline) {
        return;
    }
    // Destroy any meshes queued by releaseMesh() — safe here because the previous
    // frame's command buffer has already been submitted and the GPU is idle.
    flushPendingReleases();
    if (!m_frameInProgress) {
        beginFrame();
    }
    if (!m_frameInProgress) {
        return;
    }

    m_lastDesc = desc;

    const uint32_t frameIdx = m_currentFrame;

    // Update CameraUBO
    CameraUboData camData{};
    camData.view = desc.viewMatrix;
    camData.proj = desc.projMatrix;
    camData.cameraPos = desc.cameraPosition;
    std::memcpy(m_cameraUboMapped[frameIdx], &camData, sizeof(camData));

    // Update LightsUBO
    LightsUboData lightData{};
    if (desc.dirLight.has_value()) {
        const auto& dl = desc.dirLight.value();
        lightData.direction = dl.direction;
        lightData.intensity = dl.intensity;
        lightData.color = dl.color;
        lightData.hasLight = 1;
    }
    std::memcpy(m_lightsUboMapped[frameIdx], &lightData, sizeof(lightData));

    auto& cmd = m_commandBuffers[frameIdx];

    // Begin forward render pass (clears color + depth)
    std::array<vk::ClearValue, 2> clearValues{};
    clearValues[0] = vk::ClearColorValue{std::array<float, 4>{
        kClearColorComponent, kClearColorComponent, kClearColorComponent, 1.0F}};
    clearValues[1] = vk::ClearDepthStencilValue{1.0F, 0};

    vk::RenderPassBeginInfo rpBegin{};
    rpBegin.renderPass = m_forwardRenderPass;
    rpBegin.framebuffer = m_forwardFramebuffer;
    rpBegin.renderArea.extent = vk::Extent2D{m_offscreenWidth, m_offscreenHeight};
    rpBegin.setClearValues(clearValues);

    cmd.beginRenderPass(rpBegin, vk::SubpassContents::eInline);
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_forwardPipeline);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_forwardPipelineLayout, 0,
                           m_forwardDescriptorSets[frameIdx], {});

    // Dynamic viewport + scissor from descriptor size
    const auto vpW = static_cast<float>(m_offscreenWidth);
    const auto vpH = static_cast<float>(m_offscreenHeight);
    vk::Viewport viewport{0.0F, 0.0F, vpW, vpH, 0.0F, 1.0F};
    vk::Rect2D scissor{{0, 0}, {m_offscreenWidth, m_offscreenHeight}};
    cmd.setViewport(0, viewport);
    cmd.setScissor(0, scissor);

    // Draw each object
    for (const auto& item : desc.objects) {
        auto it = m_meshes.find(item.meshHandle);
        if (it == m_meshes.end()) {
            continue;
        }
        const auto& gpuMesh = it->second;

        ForwardPushConstants pc{};
        pc.model = item.modelMatrix;
        pc.objectId = item.objectId;
        cmd.pushConstants(m_forwardPipelineLayout, vk::ShaderStageFlagBits::eVertex, 0,
                          sizeof(pc), &pc);

        cmd.bindVertexBuffers(0, gpuMesh.vertexBuffer, vk::DeviceSize{0});
        cmd.bindIndexBuffer(gpuMesh.indexBuffer, 0, vk::IndexType::eUint32);
        cmd.drawIndexed(gpuMesh.indexCount, 1, 0, 0, 0);
    }

    cmd.endRenderPass();

    // Selection outline: mask pass + composite pass
    if (m_maskPipeline && m_compPipeline) {
        const DrawItem* selItem = nullptr;
        for (const auto& item : desc.objects) {
            if (item.isSelected) {
                selItem = &item;
                break;
            }
        }
        if (selItem != nullptr) {
            auto meshIt = m_meshes.find(selItem->meshHandle);
            if (meshIt != m_meshes.end()) {
                const auto& gm = meshIt->second;

                // 1. Mask pass: render selected object silhouette
                vk::ClearValue maskClear{
                    vk::ClearColorValue{std::array<float, 4>{0.0F, 0.0F, 0.0F, 0.0F}}};
                vk::RenderPassBeginInfo maskRp{};
                maskRp.renderPass        = m_maskRenderPass;
                maskRp.framebuffer       = m_maskFramebuffer;
                maskRp.renderArea.extent = vk::Extent2D{m_offscreenWidth, m_offscreenHeight};
                maskRp.setClearValues(maskClear);
                cmd.beginRenderPass(maskRp, vk::SubpassContents::eInline);
                cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_maskPipeline);
                cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                       m_forwardPipelineLayout, 0,
                                       m_forwardDescriptorSets[frameIdx], {});
                cmd.setViewport(0, viewport);
                cmd.setScissor(0, scissor);
                ForwardPushConstants mpc{};
                mpc.model    = selItem->modelMatrix;
                mpc.objectId = selItem->objectId;
                cmd.pushConstants(m_forwardPipelineLayout, vk::ShaderStageFlagBits::eVertex,
                                  0, sizeof(mpc), &mpc);
                cmd.bindVertexBuffers(0, gm.vertexBuffer, vk::DeviceSize{0});
                cmd.bindIndexBuffer(gm.indexBuffer, 0, vk::IndexType::eUint32);
                cmd.drawIndexed(gm.indexCount, 1, 0, 0, 0);
                cmd.endRenderPass();

                // 2. Composite pass: edge-detect mask and write orange outline
                struct CompPC {
                    glm::vec3 outlineColor{1.0F, 0.5F, 0.0F};
                    float     _pad{0.0F};
                    glm::vec2 texelSize;
                } cpc{};
                cpc.texelSize = {1.0F / static_cast<float>(m_offscreenWidth),
                                 1.0F / static_cast<float>(m_offscreenHeight)};

                vk::RenderPassBeginInfo compRp{};
                compRp.renderPass        = m_compRenderPass;
                compRp.framebuffer       = m_compFramebuffer;
                compRp.renderArea.extent = vk::Extent2D{m_offscreenWidth, m_offscreenHeight};
                cmd.beginRenderPass(compRp, vk::SubpassContents::eInline);
                cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_compPipeline);
                cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                       m_compPipelineLayout, 0, m_compDescSet, {});
                cmd.setViewport(0, viewport);
                cmd.setScissor(0, scissor);
                cmd.pushConstants(m_compPipelineLayout, vk::ShaderStageFlagBits::eFragment,
                                  0, sizeof(cpc), &cpc);
                cmd.draw(3, 1, 0, 0);
                cmd.endRenderPass();
            }
        }
    }
}

// ─── Part 8: GPU Picking ──────────────────────────────────────────────────────

namespace {
struct PickVertexInput {
    vk::VertexInputBindingDescription                 binding{0, 32, vk::VertexInputRate::eVertex};
    std::array<vk::VertexInputAttributeDescription,3> attrs{{
        {0, 0, vk::Format::eR32G32B32Sfloat,  0},
        {1, 0, vk::Format::eR32G32Sfloat,    12},
        {2, 0, vk::Format::eR32G32B32Sfloat, 20}}};
};
} // namespace

bool VulkanBackend::createPickingResources() {
    namespace fs = std::filesystem;
    try {
        // R8G8B8A8_UNORM picking image — colour attachment + transfer source for readback
        {
            vk::ImageCreateInfo imgInfo{};
            imgInfo.imageType   = vk::ImageType::e2D;
            imgInfo.format      = vk::Format::eR8G8B8A8Unorm;
            imgInfo.extent      = vk::Extent3D{m_offscreenWidth, m_offscreenHeight, 1};
            imgInfo.mipLevels   = 1;
            imgInfo.arrayLayers = 1;
            imgInfo.samples     = vk::SampleCountFlagBits::e1;
            imgInfo.tiling      = vk::ImageTiling::eOptimal;
            imgInfo.usage = vk::ImageUsageFlagBits::eColorAttachment |
                            vk::ImageUsageFlagBits::eTransferSrc;
            m_pickImage  = m_device.createImage(imgInfo);
            auto reqs    = m_device.getImageMemoryRequirements(m_pickImage);
            vk::MemoryAllocateInfo allocInfo{};
            allocInfo.allocationSize  = reqs.size;
            allocInfo.memoryTypeIndex =
                findMemoryType(reqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
            m_pickMemory = m_device.allocateMemory(allocInfo);
            m_device.bindImageMemory(m_pickImage, m_pickMemory, 0);

            vk::ImageViewCreateInfo viewInfo{};
            viewInfo.image    = m_pickImage;
            viewInfo.viewType = vk::ImageViewType::e2D;
            viewInfo.format   = vk::Format::eR8G8B8A8Unorm;
            viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.layerCount = 1;
            m_pickImageView = m_device.createImageView(viewInfo);
        }

        // Render pass: UNDEFINED → TRANSFER_SRC_OPTIMAL, clear
        {
            vk::AttachmentDescription att{};
            att.format         = vk::Format::eR8G8B8A8Unorm;
            att.samples        = vk::SampleCountFlagBits::e1;
            att.loadOp         = vk::AttachmentLoadOp::eClear;
            att.storeOp        = vk::AttachmentStoreOp::eStore;
            att.stencilLoadOp  = vk::AttachmentLoadOp::eDontCare;
            att.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
            att.initialLayout  = vk::ImageLayout::eUndefined;
            att.finalLayout    = vk::ImageLayout::eTransferSrcOptimal;

            vk::AttachmentReference ref{0, vk::ImageLayout::eColorAttachmentOptimal};
            vk::SubpassDescription  subpass{};
            subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
            subpass.setColorAttachments(ref);

            std::array<vk::SubpassDependency, 2> deps{};
            deps[0].srcSubpass    = VK_SUBPASS_EXTERNAL;
            deps[0].dstSubpass    = 0;
            deps[0].srcStageMask  = vk::PipelineStageFlagBits::eTransfer;
            deps[0].dstStageMask  = vk::PipelineStageFlagBits::eColorAttachmentOutput;
            deps[0].srcAccessMask = vk::AccessFlagBits::eTransferRead;
            deps[0].dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

            deps[1].srcSubpass    = 0;
            deps[1].dstSubpass    = VK_SUBPASS_EXTERNAL;
            deps[1].srcStageMask  = vk::PipelineStageFlagBits::eColorAttachmentOutput;
            deps[1].dstStageMask  = vk::PipelineStageFlagBits::eTransfer;
            deps[1].srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
            deps[1].dstAccessMask = vk::AccessFlagBits::eTransferRead;

            vk::RenderPassCreateInfo rpInfo{};
            rpInfo.setAttachments(att);
            rpInfo.setSubpasses(subpass);
            rpInfo.setDependencies(deps);
            m_pickRenderPass = m_device.createRenderPass(rpInfo);

            vk::FramebufferCreateInfo fbInfo{};
            fbInfo.renderPass = m_pickRenderPass;
            fbInfo.setAttachments(m_pickImageView);
            fbInfo.width  = m_offscreenWidth;
            fbInfo.height = m_offscreenHeight;
            fbInfo.layers = 1;
            m_pickFramebuffer = m_device.createFramebuffer(fbInfo);
        }

        // Picking pipeline: picking.vert + picking.frag, reuse m_forwardPipelineLayout
        {
            auto vertCode = loadSpirvFile(
                (fs::path(m_exeDir) / "shaders" / "picking.vert.spv").string());
            auto fragCode = loadSpirvFile(
                (fs::path(m_exeDir) / "shaders" / "picking.frag.spv").string());
            auto vertMod = createShaderModule(vertCode);
            auto fragMod = createShaderModule(fragCode);

            std::array<vk::PipelineShaderStageCreateInfo, 2> stages{};
            stages[0] = {vk::PipelineShaderStageCreateFlags{}, vk::ShaderStageFlagBits::eVertex,
                         vertMod, "main"};
            stages[1] = {vk::PipelineShaderStageCreateFlags{}, vk::ShaderStageFlagBits::eFragment,
                         fragMod, "main"};

            PickVertexInput pvi{};
            vk::PipelineVertexInputStateCreateInfo vi{};
            vi.setVertexBindingDescriptions(pvi.binding);
            vi.setVertexAttributeDescriptions(pvi.attrs);

            vk::PipelineInputAssemblyStateCreateInfo ia{};
            ia.topology = vk::PrimitiveTopology::eTriangleList;

            vk::PipelineViewportStateCreateInfo vpState{};
            vpState.viewportCount = 1;
            vpState.scissorCount  = 1;

            vk::PipelineRasterizationStateCreateInfo rast{};
            rast.polygonMode = vk::PolygonMode::eFill;
            rast.cullMode    = vk::CullModeFlagBits::eBack;
            rast.frontFace   = vk::FrontFace::eCounterClockwise;
            rast.lineWidth   = 1.0F;

            vk::PipelineMultisampleStateCreateInfo ms{};
            ms.rasterizationSamples = vk::SampleCountFlagBits::e1;

            vk::PipelineDepthStencilStateCreateInfo ds{};

            vk::PipelineColorBlendAttachmentState blendAtt{};
            blendAtt.colorWriteMask =
                vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
            vk::PipelineColorBlendStateCreateInfo blend{};
            blend.setAttachments(blendAtt);

            std::array<vk::DynamicState, 2> dynStates{vk::DynamicState::eViewport,
                                                       vk::DynamicState::eScissor};
            vk::PipelineDynamicStateCreateInfo dynState{};
            dynState.setDynamicStates(dynStates);

            vk::GraphicsPipelineCreateInfo pipeInfo{};
            pipeInfo.stageCount          = 2;
            pipeInfo.pStages             = stages.data();
            pipeInfo.pVertexInputState   = &vi;
            pipeInfo.pInputAssemblyState = &ia;
            pipeInfo.pViewportState      = &vpState;
            pipeInfo.pRasterizationState = &rast;
            pipeInfo.pMultisampleState   = &ms;
            pipeInfo.pDepthStencilState  = &ds;
            pipeInfo.pColorBlendState    = &blend;
            pipeInfo.pDynamicState       = &dynState;
            pipeInfo.layout              = m_forwardPipelineLayout;
            pipeInfo.renderPass          = m_pickRenderPass;
            m_pickPipeline = m_device.createGraphicsPipeline(nullptr, pipeInfo).value;

            m_device.destroyShaderModule(vertMod);
            m_device.destroyShaderModule(fragMod);
        }

        // Dedicated command pool + buffer for synchronous picking
        {
            vk::CommandPoolCreateInfo poolInfo{};
            poolInfo.queueFamilyIndex = m_graphicsFamily;
            poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
            m_pickCmdPool = m_device.createCommandPool(poolInfo);

            vk::CommandBufferAllocateInfo allocInfo{};
            allocInfo.commandPool        = m_pickCmdPool;
            allocInfo.level              = vk::CommandBufferLevel::ePrimary;
            allocInfo.commandBufferCount = 1;
            m_pickCmdBuffer = m_device.allocateCommandBuffers(allocInfo)[0];
        }

        // Host-visible staging buffer: 4 bytes (one RGBA8 pixel)
        {
            m_pickStagingBuffer = createBuffer(
                4,
                vk::BufferUsageFlagBits::eTransferDst,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
                m_pickStagingMemory);
            m_pickStagingMapped = m_device.mapMemory(m_pickStagingMemory, 0, 4);
        }

    } catch (const vk::SystemError& err) {
        SONNET_LOG_ERROR("Failed to create picking resources: {}", err.what());
        return false;
    }
    return true;
}

void VulkanBackend::destroyPickingResources() {
    if (m_pickStagingMapped) {
        m_device.unmapMemory(m_pickStagingMemory);
        m_pickStagingMapped = nullptr;
    }
    if (m_pickStagingBuffer)  { m_device.destroyBuffer(m_pickStagingBuffer);  m_pickStagingBuffer  = nullptr; }
    if (m_pickStagingMemory)  { m_device.freeMemory(m_pickStagingMemory);     m_pickStagingMemory  = nullptr; }
    if (m_pickCmdPool) {
        m_device.freeCommandBuffers(m_pickCmdPool, m_pickCmdBuffer);
        m_device.destroyCommandPool(m_pickCmdPool);
        m_pickCmdPool   = nullptr;
        m_pickCmdBuffer = nullptr;
    }
    if (m_pickPipeline)    { m_device.destroyPipeline(m_pickPipeline);        m_pickPipeline    = nullptr; }
    if (m_pickFramebuffer) { m_device.destroyFramebuffer(m_pickFramebuffer);  m_pickFramebuffer = nullptr; }
    if (m_pickRenderPass)  { m_device.destroyRenderPass(m_pickRenderPass);    m_pickRenderPass  = nullptr; }
    if (m_pickImageView)   { m_device.destroyImageView(m_pickImageView);      m_pickImageView   = nullptr; }
    if (m_pickImage)       { m_device.destroyImage(m_pickImage);              m_pickImage       = nullptr; }
    if (m_pickMemory)      { m_device.freeMemory(m_pickMemory);               m_pickMemory      = nullptr; }
}

int32_t VulkanBackend::pick(glm::ivec2 pixel) {
    if (!m_initialized || !m_pickPipeline || m_lastDesc.objects.empty()) {
        return 0;
    }
    if (pixel.x < 0 || pixel.y < 0 ||
        pixel.x >= static_cast<int32_t>(m_offscreenWidth) ||
        pixel.y >= static_cast<int32_t>(m_offscreenHeight)) {
        return 0;
    }

    m_pickCmdBuffer.reset();
    m_pickCmdBuffer.begin({vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

    vk::ClearValue clearValue{vk::ClearColorValue{std::array<float, 4>{0.0F, 0.0F, 0.0F, 0.0F}}};
    vk::RenderPassBeginInfo rpBegin{};
    rpBegin.renderPass        = m_pickRenderPass;
    rpBegin.framebuffer       = m_pickFramebuffer;
    rpBegin.renderArea.extent = vk::Extent2D{m_offscreenWidth, m_offscreenHeight};
    rpBegin.setClearValues(clearValue);
    m_pickCmdBuffer.beginRenderPass(rpBegin, vk::SubpassContents::eInline);

    m_pickCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_pickPipeline);
    m_pickCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                       m_forwardPipelineLayout, 0,
                                       m_forwardDescriptorSets[0], {});

    const auto vpW = static_cast<float>(m_offscreenWidth);
    const auto vpH = static_cast<float>(m_offscreenHeight);
    vk::Viewport viewport{0.0F, 0.0F, vpW, vpH, 0.0F, 1.0F};
    vk::Rect2D   scissor{{0, 0}, {m_offscreenWidth, m_offscreenHeight}};
    m_pickCmdBuffer.setViewport(0, viewport);
    m_pickCmdBuffer.setScissor(0, scissor);

    for (const auto& item : m_lastDesc.objects) {
        auto it = m_meshes.find(item.meshHandle);
        if (it == m_meshes.end()) {
            continue;
        }
        const auto& gpuMesh = it->second;

        ForwardPushConstants pc{};
        pc.model    = item.modelMatrix;
        pc.objectId = item.objectId;
        m_pickCmdBuffer.pushConstants(m_forwardPipelineLayout,
                                      vk::ShaderStageFlagBits::eVertex, 0, sizeof(pc), &pc);

        m_pickCmdBuffer.bindVertexBuffers(0, gpuMesh.vertexBuffer, vk::DeviceSize{0});
        m_pickCmdBuffer.bindIndexBuffer(gpuMesh.indexBuffer, 0, vk::IndexType::eUint32);
        m_pickCmdBuffer.drawIndexed(gpuMesh.indexCount, 1, 0, 0, 0);
    }

    m_pickCmdBuffer.endRenderPass();

    // Copy the single clicked pixel to the staging buffer
    vk::BufferImageCopy region{};
    region.bufferOffset      = 0;
    region.bufferRowLength   = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask     = vk::ImageAspectFlagBits::eColor;
    region.imageSubresource.mipLevel       = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount     = 1;
    region.imageOffset = vk::Offset3D{pixel.x, pixel.y, 0};
    region.imageExtent = vk::Extent3D{1, 1, 1};
    m_pickCmdBuffer.copyImageToBuffer(m_pickImage, vk::ImageLayout::eTransferSrcOptimal,
                                      m_pickStagingBuffer, region);

    m_pickCmdBuffer.end();

    vk::SubmitInfo submitInfo{};
    submitInfo.setCommandBuffers(m_pickCmdBuffer);
    m_graphicsQueue.submit(submitInfo);
    m_graphicsQueue.waitIdle();

    const auto* rgba = static_cast<const uint8_t*>(m_pickStagingMapped);
    const uint32_t id = (static_cast<uint32_t>(rgba[0]) << 16) |
                        (static_cast<uint32_t>(rgba[1]) <<  8) |
                         static_cast<uint32_t>(rgba[2]);
    return static_cast<int32_t>(id);
}

// ─── Part 9: Selection Outline ────────────────────────────────────────────────

bool VulkanBackend::createOutlineResources() {
    namespace fs = std::filesystem;
    try {
        // R8G8B8A8_UNORM mask image (R=1 for selected object pixels)
        {
            vk::ImageCreateInfo imgInfo{};
            imgInfo.imageType   = vk::ImageType::e2D;
            imgInfo.format      = vk::Format::eR8G8B8A8Unorm;
            imgInfo.extent      = vk::Extent3D{m_offscreenWidth, m_offscreenHeight, 1};
            imgInfo.mipLevels   = 1;
            imgInfo.arrayLayers = 1;
            imgInfo.samples     = vk::SampleCountFlagBits::e1;
            imgInfo.tiling      = vk::ImageTiling::eOptimal;
            imgInfo.usage = vk::ImageUsageFlagBits::eColorAttachment |
                            vk::ImageUsageFlagBits::eSampled;
            m_maskImage  = m_device.createImage(imgInfo);
            auto reqs    = m_device.getImageMemoryRequirements(m_maskImage);
            vk::MemoryAllocateInfo allocInfo{};
            allocInfo.allocationSize  = reqs.size;
            allocInfo.memoryTypeIndex =
                findMemoryType(reqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
            m_maskMemory = m_device.allocateMemory(allocInfo);
            m_device.bindImageMemory(m_maskImage, m_maskMemory, 0);

            vk::ImageViewCreateInfo viewInfo{};
            viewInfo.image    = m_maskImage;
            viewInfo.viewType = vk::ImageViewType::e2D;
            viewInfo.format   = vk::Format::eR8G8B8A8Unorm;
            viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.layerCount = 1;
            m_maskImageView = m_device.createImageView(viewInfo);

            // Transition to SHADER_READ_ONLY for initial sampling
            auto cmd = beginOneTimeCommands();
            vk::ImageMemoryBarrier barrier{};
            barrier.oldLayout        = vk::ImageLayout::eUndefined;
            barrier.newLayout        = vk::ImageLayout::eShaderReadOnlyOptimal;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image            = m_maskImage;
            barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.layerCount = 1;
            barrier.srcAccessMask    = {};
            barrier.dstAccessMask    = vk::AccessFlagBits::eShaderRead;
            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                                vk::PipelineStageFlagBits::eFragmentShader,
                                {}, {}, {}, barrier);
            endOneTimeCommands(cmd);

            vk::SamplerCreateInfo samplerInfo{};
            samplerInfo.magFilter    = vk::Filter::eNearest;
            samplerInfo.minFilter    = vk::Filter::eNearest;
            samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
            samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
            m_maskSampler = m_device.createSampler(samplerInfo);
        }

        // Mask render pass: SHADER_READ_ONLY → SHADER_READ_ONLY, clear
        {
            vk::AttachmentDescription att{};
            att.format         = vk::Format::eR8G8B8A8Unorm;
            att.samples        = vk::SampleCountFlagBits::e1;
            att.loadOp         = vk::AttachmentLoadOp::eClear;
            att.storeOp        = vk::AttachmentStoreOp::eStore;
            att.stencilLoadOp  = vk::AttachmentLoadOp::eDontCare;
            att.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
            att.initialLayout  = vk::ImageLayout::eShaderReadOnlyOptimal;
            att.finalLayout    = vk::ImageLayout::eShaderReadOnlyOptimal;

            vk::AttachmentReference ref{0, vk::ImageLayout::eColorAttachmentOptimal};
            vk::SubpassDescription  subpass{};
            subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
            subpass.setColorAttachments(ref);

            std::array<vk::SubpassDependency, 2> deps{};
            deps[0].srcSubpass      = VK_SUBPASS_EXTERNAL;
            deps[0].dstSubpass      = 0;
            deps[0].srcStageMask    = vk::PipelineStageFlagBits::eFragmentShader;
            deps[0].dstStageMask    = vk::PipelineStageFlagBits::eColorAttachmentOutput;
            deps[0].srcAccessMask   = vk::AccessFlagBits::eShaderRead;
            deps[0].dstAccessMask   = vk::AccessFlagBits::eColorAttachmentWrite;
            deps[0].dependencyFlags = vk::DependencyFlagBits::eByRegion;
            deps[1].srcSubpass      = 0;
            deps[1].dstSubpass      = VK_SUBPASS_EXTERNAL;
            deps[1].srcStageMask    = vk::PipelineStageFlagBits::eColorAttachmentOutput;
            deps[1].dstStageMask    = vk::PipelineStageFlagBits::eFragmentShader;
            deps[1].srcAccessMask   = vk::AccessFlagBits::eColorAttachmentWrite;
            deps[1].dstAccessMask   = vk::AccessFlagBits::eShaderRead;
            deps[1].dependencyFlags = vk::DependencyFlagBits::eByRegion;

            vk::RenderPassCreateInfo rpInfo{};
            rpInfo.setAttachments(att);
            rpInfo.setSubpasses(subpass);
            rpInfo.setDependencies(deps);
            m_maskRenderPass = m_device.createRenderPass(rpInfo);

            vk::FramebufferCreateInfo fbInfo{};
            fbInfo.renderPass = m_maskRenderPass;
            fbInfo.setAttachments(m_maskImageView);
            fbInfo.width  = m_offscreenWidth;
            fbInfo.height = m_offscreenHeight;
            fbInfo.layers = 1;
            m_maskFramebuffer = m_device.createFramebuffer(fbInfo);
        }

        // Mask pipeline: picking.vert + outline_mask.frag, reuse m_forwardPipelineLayout
        {
            auto vertCode = loadSpirvFile(
                (fs::path(m_exeDir) / "shaders" / "picking.vert.spv").string());
            auto fragCode = loadSpirvFile(
                (fs::path(m_exeDir) / "shaders" / "outline_mask.frag.spv").string());
            auto vertMod = createShaderModule(vertCode);
            auto fragMod = createShaderModule(fragCode);

            std::array<vk::PipelineShaderStageCreateInfo, 2> stages{};
            stages[0] = {vk::PipelineShaderStageCreateFlags{}, vk::ShaderStageFlagBits::eVertex,
                         vertMod, "main"};
            stages[1] = {vk::PipelineShaderStageCreateFlags{}, vk::ShaderStageFlagBits::eFragment,
                         fragMod, "main"};

            PickVertexInput pvi{};
            vk::PipelineVertexInputStateCreateInfo vi{};
            vi.setVertexBindingDescriptions(pvi.binding);
            vi.setVertexAttributeDescriptions(pvi.attrs);

            vk::PipelineInputAssemblyStateCreateInfo ia{};
            ia.topology = vk::PrimitiveTopology::eTriangleList;

            vk::PipelineViewportStateCreateInfo vpState{};
            vpState.viewportCount = 1;
            vpState.scissorCount  = 1;

            vk::PipelineRasterizationStateCreateInfo rast{};
            rast.polygonMode = vk::PolygonMode::eFill;
            rast.cullMode    = vk::CullModeFlagBits::eBack;
            rast.frontFace   = vk::FrontFace::eCounterClockwise;
            rast.lineWidth   = 1.0F;

            vk::PipelineMultisampleStateCreateInfo ms{};
            ms.rasterizationSamples = vk::SampleCountFlagBits::e1;

            vk::PipelineDepthStencilStateCreateInfo ds{};

            vk::PipelineColorBlendAttachmentState blendAtt{};
            blendAtt.colorWriteMask =
                vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
            vk::PipelineColorBlendStateCreateInfo blend{};
            blend.setAttachments(blendAtt);

            std::array<vk::DynamicState, 2> dynStates{vk::DynamicState::eViewport,
                                                       vk::DynamicState::eScissor};
            vk::PipelineDynamicStateCreateInfo dynState{};
            dynState.setDynamicStates(dynStates);

            vk::GraphicsPipelineCreateInfo pipeInfo{};
            pipeInfo.stageCount          = 2;
            pipeInfo.pStages             = stages.data();
            pipeInfo.pVertexInputState   = &vi;
            pipeInfo.pInputAssemblyState = &ia;
            pipeInfo.pViewportState      = &vpState;
            pipeInfo.pRasterizationState = &rast;
            pipeInfo.pMultisampleState   = &ms;
            pipeInfo.pDepthStencilState  = &ds;
            pipeInfo.pColorBlendState    = &blend;
            pipeInfo.pDynamicState       = &dynState;
            pipeInfo.layout              = m_forwardPipelineLayout;
            pipeInfo.renderPass          = m_maskRenderPass;
            m_maskPipeline = m_device.createGraphicsPipeline(nullptr, pipeInfo).value;

            m_device.destroyShaderModule(vertMod);
            m_device.destroyShaderModule(fragMod);
        }

        // Composite descriptor layout: binding 0 = combined image sampler (mask)
        {
            vk::DescriptorSetLayoutBinding b{};
            b.binding         = 0;
            b.descriptorType  = vk::DescriptorType::eCombinedImageSampler;
            b.descriptorCount = 1;
            b.stageFlags      = vk::ShaderStageFlagBits::eFragment;
            vk::DescriptorSetLayoutCreateInfo dslInfo{};
            dslInfo.setBindings(b);
            m_compDescLayout = m_device.createDescriptorSetLayout(dslInfo);

            // Push constant: {vec3 outlineColor, float _pad, vec2 texelSize} = 24 bytes
            vk::PushConstantRange pcRange{};
            pcRange.stageFlags = vk::ShaderStageFlagBits::eFragment;
            pcRange.offset     = 0;
            pcRange.size       = 24;
            vk::PipelineLayoutCreateInfo layoutInfo{};
            layoutInfo.setSetLayouts(m_compDescLayout);
            layoutInfo.setPushConstantRanges(pcRange);
            m_compPipelineLayout = m_device.createPipelineLayout(layoutInfo);
        }

        // Composite render pass: writes to offscreen image with LOAD op
        {
            vk::AttachmentDescription att{};
            att.format         = m_swapchainFormat;
            att.samples        = vk::SampleCountFlagBits::e1;
            att.loadOp         = vk::AttachmentLoadOp::eLoad;
            att.storeOp        = vk::AttachmentStoreOp::eStore;
            att.stencilLoadOp  = vk::AttachmentLoadOp::eDontCare;
            att.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
            att.initialLayout  = vk::ImageLayout::eShaderReadOnlyOptimal;
            att.finalLayout    = vk::ImageLayout::eShaderReadOnlyOptimal;

            vk::AttachmentReference ref{0, vk::ImageLayout::eColorAttachmentOptimal};
            vk::SubpassDescription  subpass{};
            subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
            subpass.setColorAttachments(ref);

            std::array<vk::SubpassDependency, 2> deps{};
            deps[0].srcSubpass    = VK_SUBPASS_EXTERNAL;
            deps[0].dstSubpass    = 0;
            deps[0].srcStageMask  =
                vk::PipelineStageFlagBits::eColorAttachmentOutput |
                vk::PipelineStageFlagBits::eFragmentShader;
            deps[0].dstStageMask  =
                vk::PipelineStageFlagBits::eColorAttachmentOutput |
                vk::PipelineStageFlagBits::eFragmentShader;
            deps[0].srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite |
                                    vk::AccessFlagBits::eShaderRead;
            deps[0].dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead |
                                    vk::AccessFlagBits::eColorAttachmentWrite |
                                    vk::AccessFlagBits::eShaderRead;
            deps[0].dependencyFlags = vk::DependencyFlagBits::eByRegion;
            deps[1].srcSubpass      = 0;
            deps[1].dstSubpass      = VK_SUBPASS_EXTERNAL;
            deps[1].srcStageMask    = vk::PipelineStageFlagBits::eColorAttachmentOutput;
            deps[1].dstStageMask    = vk::PipelineStageFlagBits::eFragmentShader;
            deps[1].srcAccessMask   = vk::AccessFlagBits::eColorAttachmentWrite;
            deps[1].dstAccessMask   = vk::AccessFlagBits::eShaderRead;
            deps[1].dependencyFlags = vk::DependencyFlagBits::eByRegion;

            vk::RenderPassCreateInfo rpInfo{};
            rpInfo.setAttachments(att);
            rpInfo.setSubpasses(subpass);
            rpInfo.setDependencies(deps);
            m_compRenderPass = m_device.createRenderPass(rpInfo);

            vk::FramebufferCreateInfo fbInfo{};
            fbInfo.renderPass = m_compRenderPass;
            fbInfo.setAttachments(m_offscreenImageView);
            fbInfo.width  = m_offscreenWidth;
            fbInfo.height = m_offscreenHeight;
            fbInfo.layers = 1;
            m_compFramebuffer = m_device.createFramebuffer(fbInfo);
        }

        // Composite pipeline: outline_comp.vert + outline_comp.frag
        {
            auto vertCode = loadSpirvFile(
                (fs::path(m_exeDir) / "shaders" / "outline_comp.vert.spv").string());
            auto fragCode = loadSpirvFile(
                (fs::path(m_exeDir) / "shaders" / "outline_comp.frag.spv").string());
            auto vertMod = createShaderModule(vertCode);
            auto fragMod = createShaderModule(fragCode);

            std::array<vk::PipelineShaderStageCreateInfo, 2> stages{};
            stages[0] = {vk::PipelineShaderStageCreateFlags{}, vk::ShaderStageFlagBits::eVertex,
                         vertMod, "main"};
            stages[1] = {vk::PipelineShaderStageCreateFlags{}, vk::ShaderStageFlagBits::eFragment,
                         fragMod, "main"};

            vk::PipelineVertexInputStateCreateInfo vi{};  // no vertex input

            vk::PipelineInputAssemblyStateCreateInfo ia{};
            ia.topology = vk::PrimitiveTopology::eTriangleList;

            vk::PipelineViewportStateCreateInfo vpState{};
            vpState.viewportCount = 1;
            vpState.scissorCount  = 1;

            vk::PipelineRasterizationStateCreateInfo rast{};
            rast.polygonMode = vk::PolygonMode::eFill;
            rast.cullMode    = vk::CullModeFlagBits::eNone;
            rast.frontFace   = vk::FrontFace::eCounterClockwise;
            rast.lineWidth   = 1.0F;

            vk::PipelineMultisampleStateCreateInfo ms{};
            ms.rasterizationSamples = vk::SampleCountFlagBits::e1;

            vk::PipelineDepthStencilStateCreateInfo ds{};

            vk::PipelineColorBlendAttachmentState blendAtt{};
            blendAtt.colorWriteMask =
                vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
            vk::PipelineColorBlendStateCreateInfo blend{};
            blend.setAttachments(blendAtt);

            std::array<vk::DynamicState, 2> dynStates{vk::DynamicState::eViewport,
                                                       vk::DynamicState::eScissor};
            vk::PipelineDynamicStateCreateInfo dynState{};
            dynState.setDynamicStates(dynStates);

            vk::GraphicsPipelineCreateInfo pipeInfo{};
            pipeInfo.stageCount          = 2;
            pipeInfo.pStages             = stages.data();
            pipeInfo.pVertexInputState   = &vi;
            pipeInfo.pInputAssemblyState = &ia;
            pipeInfo.pViewportState      = &vpState;
            pipeInfo.pRasterizationState = &rast;
            pipeInfo.pMultisampleState   = &ms;
            pipeInfo.pDepthStencilState  = &ds;
            pipeInfo.pColorBlendState    = &blend;
            pipeInfo.pDynamicState       = &dynState;
            pipeInfo.layout              = m_compPipelineLayout;
            pipeInfo.renderPass          = m_compRenderPass;
            m_compPipeline = m_device.createGraphicsPipeline(nullptr, pipeInfo).value;

            m_device.destroyShaderModule(vertMod);
            m_device.destroyShaderModule(fragMod);
        }

        // Descriptor pool + set binding mask sampler
        {
            vk::DescriptorPoolSize poolSize{vk::DescriptorType::eCombinedImageSampler, 1};
            vk::DescriptorPoolCreateInfo poolInfo{};
            poolInfo.maxSets = 1;
            poolInfo.setPoolSizes(poolSize);
            m_compDescPool = m_device.createDescriptorPool(poolInfo);

            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = m_compDescPool;
            allocInfo.setSetLayouts(m_compDescLayout);
            m_compDescSet = m_device.allocateDescriptorSets(allocInfo)[0];

            vk::DescriptorImageInfo imgInfo{};
            imgInfo.sampler     = m_maskSampler;
            imgInfo.imageView   = m_maskImageView;
            imgInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            vk::WriteDescriptorSet write{};
            write.dstSet          = m_compDescSet;
            write.dstBinding      = 0;
            write.descriptorType  = vk::DescriptorType::eCombinedImageSampler;
            write.descriptorCount = 1;
            write.setImageInfo(imgInfo);
            m_device.updateDescriptorSets(write, {});
        }

    } catch (const vk::SystemError& err) {
        SONNET_LOG_ERROR("Failed to create outline resources: {}", err.what());
        return false;
    }
    return true;
}

void VulkanBackend::destroyOutlineResources() {
    if (m_compDescPool)      { m_device.destroyDescriptorPool(m_compDescPool);       m_compDescPool      = nullptr; }
    if (m_compDescLayout)    { m_device.destroyDescriptorSetLayout(m_compDescLayout);m_compDescLayout    = nullptr; }
    if (m_compPipeline)      { m_device.destroyPipeline(m_compPipeline);             m_compPipeline      = nullptr; }
    if (m_compPipelineLayout){ m_device.destroyPipelineLayout(m_compPipelineLayout); m_compPipelineLayout= nullptr; }
    if (m_compFramebuffer)   { m_device.destroyFramebuffer(m_compFramebuffer);       m_compFramebuffer   = nullptr; }
    if (m_compRenderPass)    { m_device.destroyRenderPass(m_compRenderPass);         m_compRenderPass    = nullptr; }
    if (m_maskPipeline)      { m_device.destroyPipeline(m_maskPipeline);             m_maskPipeline      = nullptr; }
    if (m_maskFramebuffer)   { m_device.destroyFramebuffer(m_maskFramebuffer);       m_maskFramebuffer   = nullptr; }
    if (m_maskRenderPass)    { m_device.destroyRenderPass(m_maskRenderPass);         m_maskRenderPass    = nullptr; }
    if (m_maskSampler)       { m_device.destroySampler(m_maskSampler);               m_maskSampler       = nullptr; }
    if (m_maskImageView)     { m_device.destroyImageView(m_maskImageView);           m_maskImageView     = nullptr; }
    if (m_maskImage)         { m_device.destroyImage(m_maskImage);                   m_maskImage         = nullptr; }
    if (m_maskMemory)        { m_device.freeMemory(m_maskMemory);                    m_maskMemory        = nullptr; }
}

// ─── Part 10: Factory ─────────────────────────────────────────────────────────

std::unique_ptr<IRendererBackend> createVulkanBackend() {
    return std::make_unique<VulkanBackend>();
}

} // namespace sonnet::renderer
