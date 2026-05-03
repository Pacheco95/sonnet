#include "SceneHierarchyPanel.hpp"

#include <imgui.h>

namespace sonnet::editor {

const char* SceneHierarchyPanel::title() const {
    return "Scene Hierarchy";
}

void SceneHierarchyPanel::draw() {
    ImGui::Text("(empty)");
}

} // namespace sonnet::editor
