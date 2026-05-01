#include "VulkanBackend.hpp"

#include <sonnet/logging/Logger.hpp>
#include <sonnet/renderer/VulkanBackendFactory.hpp>
#include <sonnet/window/IWindow.hpp>

#include <algorithm>
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
    shutdown();
} // NOLINT(clang-analyzer-optin.cplusplus.VirtualCall)

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

// ─── Part 6: Factory ──────────────────────────────────────────────────────────

std::unique_ptr<IRendererBackend> createVulkanBackend() {
    return std::make_unique<VulkanBackend>();
}

} // namespace sonnet::renderer
