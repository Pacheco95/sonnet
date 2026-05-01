#pragma once

namespace sonnet::window { class IWindow; }

namespace sonnet::renderer {

class IRenderer {
public:
    virtual ~IRenderer() = default;

    virtual bool init(sonnet::window::IWindow& window) = 0;
    virtual void shutdown() = 0;
    virtual void renderFrame() = 0;
};

} // namespace sonnet::renderer
