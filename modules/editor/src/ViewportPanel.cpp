#include <sonnet/editor/ViewportPanel.h>

#include <algorithm>
#include <cmath>

namespace sonnet::editor {

ViewportPanel::ViewportPanel(rhi::IDevice &device, ui::ImGuiLayer &imgui)
    : m_imgui(imgui), m_target(device, "viewport") {
}

ViewportPanel::~ViewportPanel() {
  m_imgui.unregisterImage(m_texture);
}

void ViewportPanel::resizeTarget(glm::uvec2 size) {
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

bool ViewportPanel::draw(bool &open, float dt, glm::vec2 lookDelta, StatisticsPanel *statistics,
                         const std::function<void(const ViewportInput &)> &overlay) {
  m_input = {};
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
  const bool visible = ImGui::Begin("Viewport", &open);
  ImGui::PopStyleVar();
  if (!visible) {
    ImGui::End();
    resizeTarget({0, 0});
    m_cameraActive = false;
    return false;
  }

  const ImVec2 available = ImGui::GetContentRegionAvail();
  const glm::uvec2 size{static_cast<unsigned>(std::max(available.x, 0.0f)),
                        static_cast<unsigned>(std::max(available.y, 0.0f))};
  resizeTarget(size);

  const ImVec2 origin = ImGui::GetCursorScreenPos();
  if (m_target.isValid()) {
    ImGui::Image(m_texture, available);
  } else {
    ImGui::Dummy(available);
  }
  const bool hovered = ImGui::IsItemHovered();
  const ImVec2 mouse = ImGui::GetMousePos();
  m_input = ViewportInput{.visible = true,
                          .hovered = hovered,
                          .focused = ImGui::IsWindowFocused(),
                          .origin = {origin.x, origin.y},
                          .size = {available.x, available.y},
                          .mouse = {mouse.x, mouse.y},
                          .leftClicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left),
                          .leftDown = ImGui::IsMouseDown(ImGuiMouseButton_Left),
                          .drawList = ImGui::GetWindowDrawList()};

  // Right mouse over the viewport takes the camera; releasing anywhere gives it back.
  if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
    m_cameraActive = true;
  }
  if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
    m_cameraActive = false;
  }
  if (m_cameraActive) {
    const float wheel = ImGui::GetIO().MouseWheel;
    if (wheel != 0.0f) {
      m_camera.scaleSpeed(std::pow(1.2f, wheel));
    }
    m_camera.update(dt, FlyCamera::Input{.lookDelta = lookDelta,
                                         .forward = ImGui::IsKeyDown(ImGuiKey_W),
                                         .back = ImGui::IsKeyDown(ImGuiKey_S),
                                         .left = ImGui::IsKeyDown(ImGuiKey_A),
                                         .right = ImGui::IsKeyDown(ImGuiKey_D),
                                         .up = ImGui::IsKeyDown(ImGuiKey_E),
                                         .down = ImGui::IsKeyDown(ImGuiKey_Q),
                                         .fast = ImGui::IsKeyDown(ImGuiKey_LeftShift)});
  }

  if (overlay) {
    overlay(m_input);
  }
  if (statistics != nullptr) {
    ImGui::SetCursorScreenPos(ImVec2{origin.x + 8.0f, origin.y + 8.0f});
    statistics->drawOverlay();
  }
  ImGui::End();
  return m_cameraActive;
}

void ViewportPanel::focus(glm::vec3 target, float radius) {
  const float distance = std::max(radius, 0.5f) * 2.5f;
  m_camera.lookAt(target - m_camera.camera().forward() * distance, target);
}

glm::uvec2 ViewportPanel::targetPixel(glm::vec2 screen) const {
  if (m_input.size.x <= 0.0f || m_input.size.y <= 0.0f) {
    return {0, 0};
  }
  const glm::vec2 relative = (screen - m_input.origin) / m_input.size;
  const glm::vec2 pixel = glm::clamp(relative, glm::vec2{0.0f}, glm::vec2{1.0f}) * glm::vec2{m_target.size()};
  return {static_cast<unsigned>(pixel.x), static_cast<unsigned>(pixel.y)};
}

} // namespace sonnet::editor
