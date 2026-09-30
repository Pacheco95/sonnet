#include <sonnet/editor/GamePanel.h>

#include <algorithm>

namespace sonnet::editor {

GamePanel::GamePanel(rhi::IDevice &device, ui::ImGuiLayer &imgui) : m_imgui(imgui), m_target(device, "game view") {
}

GamePanel::~GamePanel() {
  m_imgui.unregisterImage(m_texture);
}

void GamePanel::resizeTarget(glm::uvec2 size) {
  if (size == m_target.size()) {
    return;
  }
  m_imgui.unregisterImage(m_texture);
  m_texture = 0;
  m_target.resize(size);
  if (m_target.isValid()) {
    m_texture = m_imgui.registerImage(m_target.color());
  }
}

void GamePanel::hide() {
  m_input = {};
  resizeTarget({0, 0});
}

void GamePanel::draw(bool &open) {
  m_input = {};
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
  const bool visible = ImGui::Begin("Game", &open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar();
  if (!visible) {
    ImGui::End();
    resizeTarget({0, 0});
    return;
  }

  const ImVec2 available = ImGui::GetContentRegionAvail();
  resizeTarget(
      {static_cast<unsigned>(std::max(available.x, 0.0f)), static_cast<unsigned>(std::max(available.y, 0.0f))});
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  if (m_target.isValid()) {
    ImGui::Image(m_texture, available);
  } else {
    ImGui::Dummy(available);
  }
  const bool hovered = ImGui::IsItemHovered();
  const ImVec2 mouse = ImGui::GetMousePos();
  m_input = ViewportInput{.visible = m_target.isValid(),
                          .hovered = hovered,
                          .focused = ImGui::IsWindowFocused(),
                          .origin = {origin.x, origin.y},
                          .size = {available.x, available.y},
                          .mouse = {mouse.x, mouse.y},
                          .leftClicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left),
                          .leftDoubleClicked = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left),
                          .leftDown = ImGui::IsMouseDown(ImGuiMouseButton_Left),
                          .drawList = ImGui::GetWindowDrawList()};
  ImGui::End();
}

} // namespace sonnet::editor
