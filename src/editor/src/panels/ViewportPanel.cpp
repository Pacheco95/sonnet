#include "ViewportPanel.hpp"

#include <imgui.h>

namespace sonnet::editor {

ViewportPanel::ViewportPanel(uint64_t viewportTextureId)
    : m_viewportTextureId(viewportTextureId) {}

const char* ViewportPanel::title() const {
    return "Viewport";
}

void ViewportPanel::draw() {
    ImVec2 size = ImGui::GetContentRegionAvail();
    if (size.x < 1.0F) {
        size.x = 1.0F;
    }
    if (size.y < 1.0F) {
        size.y = 1.0F;
    }
    ImGui::Image(static_cast<ImTextureID>(m_viewportTextureId), size);
}

} // namespace sonnet::editor
