#pragma once

namespace sonnet::window {
class IWindow;
}

namespace sonnet::renderer {
class IRendererBackend;
}

namespace sonnet::editor {

class IEditor {
public:
    virtual ~IEditor() = default;

    // Initialises editor: sets up ImGui context, registers log sink,
    // loads active layout. Returns false on failure.
    virtual bool init(sonnet::window::IWindow& window,
                      sonnet::renderer::IRendererBackend& backend) = 0;

    virtual void shutdown() = 0;

    // Processes a platform event before ImGui; returns true if ImGui consumed it.
    virtual bool processEvent(void* sdlEvent) = 0;

    // Called once per frame: runs all panel UIs and renders ImGui draw data.
    virtual void render() = 0;
};

} // namespace sonnet::editor
