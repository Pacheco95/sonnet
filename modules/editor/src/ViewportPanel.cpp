#include <sonnet/editor/ViewportPanel.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace sonnet::editor {

namespace {

// Focusing never views the object from lower than this many radians below the horizon.
constexpr float MinFocusPitch = glm::radians(-15.0f);

} // namespace

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
                         const std::function<void(const ViewportInput &)> &overlay,
                         const std::function<void()> &header) {
  m_input = {};
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
  // The image fills the window, so the wheel must not scroll it: it dollies the camera.
  const bool visible =
      ImGui::Begin("Viewport", &open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar();
  if (!visible) {
    ImGui::End();
    resizeTarget({0, 0});
    m_cameraActive = false;
    return false;
  }

  if (header) {
    header();
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
                          .leftDoubleClicked = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left),
                          .leftDown = ImGui::IsMouseDown(ImGuiMouseButton_Left),
                          .drawList = ImGui::GetWindowDrawList()};

  // Right mouse over the viewport takes the camera to fly, middle mouse to orbit; releasing that
  // button anywhere gives it back.
  if (!m_cameraActive && hovered) {
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
      m_cameraActive = true;
      m_orbiting = false;
    } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
      m_cameraActive = true;
      m_orbiting = true;
      if (!m_view.hasPivot) {
        m_view.pivot = m_view.camera.camera().position + m_view.camera.camera().forward() * m_view.focusDistance;
        m_view.hasPivot = true;
      }
    }
  }
  if (!ImGui::IsMouseDown(m_orbiting ? ImGuiMouseButton_Middle : ImGuiMouseButton_Right)) {
    m_cameraActive = false;
    m_orbiting = false;
  }
  if ((hovered || m_input.focused) && !ImGui::GetIO().WantTextInput && !m_wheelForGame) {
    const bool ctrl = ImGui::GetIO().KeyCtrl;
    if (ImGui::IsKeyPressed(ImGuiKey_Keypad1, false)) {
      snapView(ctrl ? AxisView::Back : AxisView::Front);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Keypad3, false)) {
      snapView(ctrl ? AxisView::Left : AxisView::Right);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Keypad7, false)) {
      snapView(ctrl ? AxisView::Bottom : AxisView::Top);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Keypad9, false)) {
      oppositeView();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Keypad5, false)) {
      toggleProjection();
    }
  }
  const float wheel = ImGui::GetIO().MouseWheel;
  if (!m_cameraActive && hovered && !m_wheelForGame && wheel != 0.0f) {
    const float distance =
        m_view.hasFocusTarget ? glm::length(m_view.focusTarget - m_view.camera.camera().position) : 0.0f;
    if (m_view.camera.camera().projectionKind == renderer::ProjectionKind::Orthographic) {
      const glm::vec3 before = m_view.camera.camera().position;
      m_view.camera.zoom(wheel, (m_input.mouse - m_input.origin) / glm::max(m_input.size, glm::vec2{1.0f}),
                         m_input.size.x / std::max(m_input.size.y, 1.0f));
      const glm::vec3 pan = m_view.camera.camera().position - before;
      m_view.pivot += pan;
      m_view.focusTarget += pan;
    } else {
      m_view.camera.dolly(wheel, distance,
                          ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift));
    }
  }
  if (m_cameraActive && m_orbiting) {
    m_view.camera.orbit(lookDelta, m_view.pivot);
  } else if (m_cameraActive) {
    if (wheel != 0.0f) {
      m_view.camera.scaleSpeed(std::pow(1.2f, wheel));
    }
    const FlyCamera::Input flight{.lookDelta = lookDelta,
                                  .forward = ImGui::IsKeyDown(ImGuiKey_W),
                                  .back = ImGui::IsKeyDown(ImGuiKey_S),
                                  .left = ImGui::IsKeyDown(ImGuiKey_A),
                                  .right = ImGui::IsKeyDown(ImGuiKey_D),
                                  .up = ImGui::IsKeyDown(ImGuiKey_E),
                                  .down = ImGui::IsKeyDown(ImGuiKey_Q),
                                  .fast = ImGui::IsKeyDown(ImGuiKey_LeftShift)};
    if (flight.forward || flight.back || flight.left || flight.right || flight.up || flight.down) {
      m_view.hasPivot = false; // flown away: the next orbit picks a new pivot in front of the camera
    }
    const glm::vec3 before = m_view.camera.camera().position;
    m_view.camera.update(dt, flight);
    if (m_view.camera.camera().projectionKind == renderer::ProjectionKind::Orthographic) {
      const glm::vec3 pan = m_view.camera.camera().position - before;
      m_view.pivot += pan;
      m_view.focusTarget += pan;
    }
  }

  const std::string label =
      std::string{m_view.camera.viewName()} +
      (m_view.camera.camera().projectionKind == renderer::ProjectionKind::Orthographic ? " Orthographic"
                                                                                       : " Perspective");
  ImGui::GetWindowDrawList()->AddText(ImVec2{origin.x + 8.0f, origin.y + available.y - 24.0f},
                                      IM_COL32(240, 240, 240, 255), label.c_str());
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

glm::vec3 ViewportPanel::viewPivot() const {
  if (m_view.hasPivot) {
    return m_view.pivot;
  }
  if (m_view.hasFocusTarget) {
    return m_view.focusTarget;
  }
  return m_view.camera.camera().position + m_view.camera.camera().forward() * m_view.focusDistance;
}

void ViewportPanel::snapView(AxisView view) {
  m_view.pivot = viewPivot();
  m_view.hasPivot = true;
  m_view.camera.snap(view, m_view.pivot);
}

void ViewportPanel::oppositeView() {
  m_view.camera.opposite(viewPivot());
}
void ViewportPanel::toggleProjection() {
  m_view.camera.toggleProjection(viewPivot());
}

void ViewportPanel::drawViewMenu() {
  if (ImGui::BeginMenu("Axis view")) {
    const AxisView views[] = {AxisView::Front, AxisView::Back, AxisView::Right,
                              AxisView::Left,  AxisView::Top,  AxisView::Bottom};
    const char *names[] = {"Front", "Back", "Right", "Left", "Top", "Bottom"};
    const char *keys[] = {"Numpad 1", "Ctrl+Numpad 1", "Numpad 3", "Ctrl+Numpad 3", "Numpad 7", "Ctrl+Numpad 7"};
    for (int i = 0; i < 6; ++i) {
      if (ImGui::MenuItem(names[i], keys[i], m_view.camera.axisView() == views[i])) {
        snapView(views[i]);
      }
    }
    if (ImGui::MenuItem("Opposite", "Numpad 9", false, m_view.camera.axisView() != AxisView::User)) {
      oppositeView();
    }
    ImGui::EndMenu();
  }
  if (ImGui::MenuItem("Orthographic", "Numpad 5",
                      m_view.camera.camera().projectionKind == renderer::ProjectionKind::Orthographic)) {
    toggleProjection();
  }
}

void ViewportPanel::focus(glm::vec3 target, float radius) {
  const float distance = std::max(radius, 0.5f) * 2.5f;
  // Keep the heading but look down at least a little, so a camera turned to the sky ends above
  // the object rather than beneath it.
  const float yaw = m_view.camera.yaw();
  const float pitch = std::min(m_view.camera.pitch(), MinFocusPitch);
  const glm::vec3 forward{-std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};
  if (m_view.camera.camera().projectionKind == renderer::ProjectionKind::Orthographic) {
    m_view.camera.frame(target, distance);
  } else {
    m_view.camera.lookAt(target - forward * distance, target);
  }
  m_view.focusTarget = target;
  m_view.hasFocusTarget = true;
  m_view.pivot = target;
  m_view.hasPivot = true;
  m_view.focusDistance = distance;
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
