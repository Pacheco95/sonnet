#pragma once

#include <sonnet/renderer/IRendererBackend.hpp>

#include <vulkan/vulkan.hpp>

#include <cstdint>
#include <unordered_map>
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

    [[nodiscard]] uint64_t uploadMesh(const CPUMesh& mesh) override;
    void releaseMesh(uint64_t handle) override;
    void renderScene(const SceneRenderDesc& desc) override;
    [[nodiscard]] int32_t pick(glm::ivec2 pixel) override;
    [[nodiscard]] glm::ivec2 getOffscreenSize() const override;

private:
    struct GpuMesh {
        vk::Buffer       vertexBuffer;
        vk::DeviceMemory vertexMemory;
        vk::Buffer       indexBuffer;
        vk::DeviceMemory indexMemory;
        uint32_t         indexCount{0};
    };
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

    static constexpr uint32_t kMaxFramesInFlight = 1;
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

    // Forward lit pipeline
    bool createForwardPipeline();
    void destroyForwardResources();
    vk::Buffer createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage,
                             vk::MemoryPropertyFlags props, vk::DeviceMemory& outMem);

    vk::RenderPass          m_forwardRenderPass;
    vk::DescriptorSetLayout m_forwardDescriptorSetLayout;
    vk::PipelineLayout      m_forwardPipelineLayout;
    vk::Pipeline            m_forwardPipeline;
    vk::DescriptorPool      m_forwardDescriptorPool;
    std::vector<vk::DescriptorSet> m_forwardDescriptorSets;

    vk::Image        m_depthImage;
    vk::DeviceMemory m_depthMemory;
    vk::ImageView    m_depthImageView;
    vk::Framebuffer  m_forwardFramebuffer;

    std::vector<vk::Buffer>       m_cameraUboBuffers;
    std::vector<vk::DeviceMemory> m_cameraUboMemories;
    std::vector<void*>            m_cameraUboMapped;
    std::vector<vk::Buffer>       m_lightsUboBuffers;
    std::vector<vk::DeviceMemory> m_lightsUboMemories;
    std::vector<void*>            m_lightsUboMapped;

    std::unordered_map<uint64_t, GpuMesh> m_meshes;
    uint64_t m_nextMeshHandle{1};
    std::vector<uint64_t> m_pendingRelease;

    void flushPendingReleases();

    SceneRenderDesc m_lastDesc;

    // GPU picking
    bool createPickingResources();
    void destroyPickingResources();

    vk::Image           m_pickImage;
    vk::DeviceMemory    m_pickMemory;
    vk::ImageView       m_pickImageView;
    vk::RenderPass      m_pickRenderPass;
    vk::Framebuffer     m_pickFramebuffer;
    vk::Pipeline        m_pickPipeline;
    vk::CommandPool     m_pickCmdPool;
    vk::CommandBuffer   m_pickCmdBuffer;
    vk::Buffer          m_pickStagingBuffer;
    vk::DeviceMemory    m_pickStagingMemory;
    void*               m_pickStagingMapped{nullptr};

    // Selection outline
    bool createOutlineResources();
    void destroyOutlineResources();

    vk::Image               m_maskImage;
    vk::DeviceMemory        m_maskMemory;
    vk::ImageView           m_maskImageView;
    vk::Sampler             m_maskSampler;
    vk::RenderPass          m_maskRenderPass;
    vk::Framebuffer         m_maskFramebuffer;
    vk::Pipeline            m_maskPipeline;
    vk::RenderPass          m_compRenderPass;
    vk::Framebuffer         m_compFramebuffer;
    vk::Pipeline            m_compPipeline;
    vk::PipelineLayout      m_compPipelineLayout;
    vk::DescriptorSetLayout m_compDescLayout;
    vk::DescriptorPool      m_compDescPool;
    vk::DescriptorSet       m_compDescSet;
};

} // namespace sonnet::renderer
