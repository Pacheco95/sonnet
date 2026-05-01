#pragma once

namespace sonnet::window {
class IWindow;
}

namespace sonnet::renderer {

class IRendererBackend {
public:
    virtual ~IRendererBackend() = default;

    virtual bool init(sonnet::window::IWindow& window) = 0;
    virtual void shutdown() = 0;

    virtual void beginFrame() = 0;
    virtual void drawPrimitive() = 0;
    virtual void endFrame() = 0;
};

} // namespace sonnet::renderer
