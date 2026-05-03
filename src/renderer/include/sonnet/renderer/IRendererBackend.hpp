#pragma once

#include <cstdint>

namespace sonnet::window {
class IWindow;
}

namespace sonnet::renderer {

struct RendererNativeHandles {
    uint64_t instance{0};
    uint64_t physicalDevice{0};
    uint64_t device{0};
    uint64_t graphicsQueue{0};
    uint64_t renderPass{0};
    uint64_t descriptorPool{0};
    uint32_t graphicsQueueFamily{0};
    uint32_t imageCount{0};
};

class IRendererBackend {
public:
    virtual ~IRendererBackend() = default;

    virtual bool init(sonnet::window::IWindow& window) = 0;
    virtual void shutdown() = 0;

    virtual void beginFrame() = 0;
    virtual void drawPrimitive() = 0;
    virtual void endFrame() = 0;

    // Returns native GPU handles for editor UI initialization.
    // Returns a zero-initialized struct if the backend does not support editor integration.
    [[nodiscard]] virtual RendererNativeHandles getNativeHandles() const { return {}; }

    // Renders the scene to the offscreen framebuffer. No-op in non-editor backends.
    virtual void renderSceneOffscreen() {}

    // Returns the offscreen scene texture ID (VkDescriptorSet cast to uint64_t).
    // Returns 0 if offscreen rendering is not available.
    [[nodiscard]] virtual uint64_t getViewportTextureId() const { return 0; }

    // Editor render pass helpers: the Editor calls these to bracket ImGui draw recording.
    virtual void beginEditorRenderPass() {}
    virtual void endEditorRenderPass() {}

    // Returns the current frame's command buffer as uint64_t (VkCommandBuffer).
    [[nodiscard]] virtual uint64_t getCurrentCommandBuffer() const { return 0; }
};

} // namespace sonnet::renderer
