#pragma once

#include <sonnet/renderer/IRendererBackend.hpp>

#include <vulkan/vulkan.hpp>

#include <cstdint>
#include <vector>

namespace sonnet::renderer {

class VulkanBackend : public IRendererBackend {
public:
    VulkanBackend() = default;
    ~VulkanBackend() override;

    bool init(sonnet::window::IWindow& window) override;
    void shutdown() override;

    void beginFrame() override;
    void drawPrimitive() override;
    void endFrame() override;

    [[nodiscard]] RendererNativeHandles getNativeHandles() const override;
    void renderSceneOffscreen() override;
    [[nodiscard]] uint64_t getViewportTextureId() const override;
    void beginEditorRenderPass() override;
    void endEditorRenderPass() override;
    [[nodiscard]] uint64_t getCurrentCommandBuffer() const override;

private:
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT types,
        const VkDebugUtilsMessengerCallbackDataEXT* data, void* userData);

    bool createInstance(const std::vector<std::string>& windowExtensions);
    bool createSurface(sonnet::window::IWindow& window);
    bool selectPhysicalDevice();
    bool createLogicalDevice();
    bool createSwapchain(sonnet::window::IWindow& window);
    bool createRenderPass();
    bool createPipeline(const std::string& exeDir);
    bool createFramebuffers();
    bool createCommandPool();
    bool createSyncObjects();

    void destroySwapchainResources();
    bool rebuildSwapchain();

    bool createOffscreenResources();
    void destroyOffscreenResources();
    bool createImguiDescriptorPool();
    [[nodiscard]] uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) const;
    [[nodiscard]] vk::CommandBuffer beginOneTimeCommands() const;
    void endOneTimeCommands(vk::CommandBuffer cmd) const;

    vk::DescriptorSetLayout m_viewportDescriptorSetLayout;

    static std::vector<char> loadSpirvFile(const std::string& path);
    vk::ShaderModule createShaderModule(const std::vector<char>& code);

    vk::Instance m_instance;
    vk::DebugUtilsMessengerEXT m_debugMessenger;
    vk::SurfaceKHR m_surface;
    vk::PhysicalDevice m_physicalDevice;
    vk::Device m_device;
    vk::Queue m_graphicsQueue;
    vk::Queue m_presentQueue;
    uint32_t m_graphicsFamily{0};
    uint32_t m_presentFamily{0};

    vk::SwapchainKHR m_swapchain;
    std::vector<vk::Image> m_swapchainImages;
    std::vector<vk::ImageView> m_swapchainImageViews;
    vk::Format m_swapchainFormat{};
    vk::Extent2D m_swapchainExtent{};

    vk::RenderPass m_renderPass;
    vk::PipelineLayout m_pipelineLayout;
    vk::Pipeline m_pipeline;

    std::vector<vk::Framebuffer> m_framebuffers;
    vk::CommandPool m_commandPool;
    std::vector<vk::CommandBuffer> m_commandBuffers;

    static constexpr uint32_t kMaxFramesInFlight = 2;
    std::vector<vk::Semaphore> m_imageAvailableSemaphores;
    std::vector<vk::Semaphore> m_renderFinishedSemaphores;
    std::vector<vk::Fence> m_inFlightFences;

    uint32_t m_currentFrame{0};
    uint32_t m_currentImageIndex{0};
    bool m_frameInProgress{false};

    // Offscreen scene render target
    vk::Image m_offscreenImage;
    vk::DeviceMemory m_offscreenMemory;
    vk::ImageView m_offscreenImageView;
    vk::RenderPass m_offscreenRenderPass;
    vk::Framebuffer m_offscreenFramebuffer;
    vk::Sampler m_offscreenSampler;
    vk::DescriptorSet m_offscreenDescriptorSet;
    vk::DescriptorPool m_imguiDescriptorPool;
    uint32_t m_offscreenWidth{0};
    uint32_t m_offscreenHeight{0};

    std::string m_exeDir;
    bool m_initialized{false};
    sonnet::window::IWindow* m_window{nullptr};
};

} // namespace sonnet::renderer
