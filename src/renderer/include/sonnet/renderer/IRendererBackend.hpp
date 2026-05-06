#pragma once

#include <sonnet/renderer/CPUMesh.hpp>
#include <sonnet/renderer/SceneRenderTypes.hpp>

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

    // Scene rendering. uploadMesh returns a handle (0 = failure sentinel). releaseMesh frees it.
    [[nodiscard]] virtual uint64_t uploadMesh(const CPUMesh& /*mesh*/) { return 0; }
    virtual void releaseMesh(uint64_t /*handle*/) {}

    // Renders all DrawItems with lighting and selection outline to the offscreen render target.
    virtual void renderScene(const SceneRenderDesc& /*desc*/) {}

    // GPU object picking: renders an ID-color pass and reads back the pixel at the given
    // viewport coordinate. Returns the 1-based objectId under the cursor, or 0 for a miss.
    [[nodiscard]] virtual int32_t pick(glm::ivec2 /*pixel*/) { return 0; }

    // Returns the size in pixels of the offscreen render target used by getViewportTextureId().
    [[nodiscard]] virtual glm::ivec2 getOffscreenSize() const { return {0, 0}; }
};

} // namespace sonnet::renderer
