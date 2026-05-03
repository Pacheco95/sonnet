#include "InspectorPanel.hpp"

#include <imgui.h>

namespace sonnet::editor {

const char* InspectorPanel::title() const { return "Inspector"; }

void InspectorPanel::draw() { ImGui::Text("(empty)"); }

} // namespace sonnet::editor
