#pragma once

namespace sonnet::editor {

class IPanel {
public:
    virtual ~IPanel() = default;

    // Returns the human-readable panel title shown in the ImGui title bar / tab.
    [[nodiscard]] virtual const char* title() const = 0;

    // Returns false if this panel may be closed by the user.
    [[nodiscard]] virtual bool isPermanent() const { return false; }

    // Draws this panel's ImGui content. Called between ImGui::Begin/End by the editor.
    virtual void draw() = 0;
};

} // namespace sonnet::editor
