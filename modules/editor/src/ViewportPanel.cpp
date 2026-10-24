#include <sonnet/editor/ViewportPanel.h>

#include <sonnet/ui/Theme.h>
#include <sonnet/ui/Widgets.h>

#include <algorithm>
#include <array>
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
  m_view.camera.advanceTransition(dt);
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
  bool hovered = ImGui::IsItemHovered();
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

  if (drawAxisWidget(origin, available)) {
    hovered = false;
    m_input.hovered = false;
    m_input.leftClicked = false;
    m_input.leftDoubleClicked = false;
    m_input.leftDown = false;
  }

  if ((hovered || m_input.focused) && !ImGui::GetIO().WantTextInput) {
    bool keyPressed = false;
    for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; ++key) {
      // View commands retarget the animation themselves. F is handled by Editor before draw.
      const bool viewCommand = key == ImGuiKey_F || key == ImGuiKey_Keypad1 || key == ImGuiKey_Keypad3 ||
                               key == ImGuiKey_Keypad5 || key == ImGuiKey_Keypad7 || key == ImGuiKey_Keypad9;
      if (!viewCommand && (key < ImGuiKey_MouseLeft || key > ImGuiKey_MouseWheelY)) {
        keyPressed = keyPressed || ImGui::IsKeyPressed(static_cast<ImGuiKey>(key), false);
      }
    }
    if (keyPressed ||
        (hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
                     ImGui::IsMouseClicked(ImGuiMouseButton_Middle) || ImGui::GetIO().MouseWheel != 0.0f))) {
      cancelTransition();
    }
  }

  // A mouse button pressed over the viewport takes the camera; releasing it gives it back. Right
  // flies, middle pans, and Alt + left orbits (the left press is the camera's then, not a pick or
  // a gizmo drag, and the drag goes on if Alt is let go first).
  const bool rightDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
  const bool middleDown = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
  if (!m_cameraActive && hovered) {
    if (ImGui::GetIO().KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      m_cameraActive = true;
      m_altOrbit = true;
    } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
      m_cameraActive = true;
    }
  }
  if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    m_altOrbit = false;
  }
  if (!rightDown && !middleDown && !m_altOrbit) {
    m_cameraActive = false;
  }
  if (m_altOrbit || (hovered && ImGui::GetIO().KeyAlt)) {
    m_input.leftClicked = false;
    m_input.leftDoubleClicked = false;
    m_input.leftDown = false;
  }
  m_drag = m_altOrbit ? Drag::Orbit : rightDown ? Drag::Fly : Drag::Pan;
  if (m_cameraActive && m_drag == Drag::Orbit && !m_view.hasPivot) {
    m_view.pivot = m_view.camera.camera().position + m_view.camera.camera().forward() * m_view.focusDistance;
    m_view.hasPivot = true;
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
      const float aspect = m_input.size.x / std::max(m_input.size.y, 1.0f);
      if (m_view.hasFocusTarget) {
        // Like the perspective dolly, zoom about the focused object: it keeps its place on screen.
        const renderer::Camera &camera = m_view.camera.camera();
        const glm::vec4 clip = camera.projection(aspect) * camera.view() * glm::vec4{m_view.focusTarget, 1.0f};
        const glm::vec2 fraction{clip.x * 0.5f + 0.5f, 0.5f - clip.y * 0.5f};
        m_view.camera.zoom(wheel, fraction, aspect);
      } else {
        const glm::vec3 before = m_view.camera.camera().position;
        m_view.camera.zoom(wheel, (m_input.mouse - m_input.origin) / glm::max(m_input.size, glm::vec2{1.0f}), aspect);
        const glm::vec3 pan = m_view.camera.camera().position - before;
        m_view.pivot += pan;
      }
    } else {
      m_view.camera.dolly(wheel, distance,
                          ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift));
    }
  }
  if (m_cameraActive && m_drag == Drag::Orbit) {
    m_view.camera.orbit(lookDelta, m_view.pivot);
  } else if (m_cameraActive && m_drag == Drag::Pan) {
    // The pivot and the focused object travel with the camera, so a later orbit or dolly keeps its target.
    const glm::vec3 pivotPoint =
        m_view.hasPivot ? m_view.pivot
                        : m_view.camera.camera().position + m_view.camera.camera().forward() * m_view.focusDistance;
    const glm::vec3 offset = m_view.camera.pan(
        lookDelta, glm::dot(pivotPoint - m_view.camera.camera().position, m_view.camera.camera().forward()),
        m_input.size.y);
    m_view.pivot += offset;
    m_view.focusTarget += offset;
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
  ui::overlayLabel(*ImGui::GetWindowDrawList(), origin, available, label.c_str());
  if (overlay) {
    overlay(m_input);
  }
  if (statistics != nullptr) {
    const ImVec2 inset = ui::overlayInset();
    ImGui::SetCursorScreenPos(ImVec2{origin.x + inset.x, origin.y + inset.y});
    statistics->drawOverlay();
  }
  ImGui::End();
  return m_cameraActive;
}

bool ViewportPanel::drawAxisWidget(ImVec2 origin, ImVec2 size) {
  const float unit = ImGui::GetFontSize();
  const float radius = unit * 0.65f;
  const float arm = unit * 2.1f;
  const float extent = arm + radius + unit * 0.5f;
  // Keep the controls inside the image, including at larger UI scales.
  if (size.x < extent * 2.0f + unit * 2.0f || size.y < extent * 2.0f) {
    return false;
  }
  const ImVec2 center{origin.x + size.x - extent, origin.y + extent};
  struct End {
    ImVec2 position;
    float depth;
    AxisView view;
    ImU32 color;
    ImU32 highlight;
    const char *label;
    bool positive;
  };
  std::array<End, 6> ends;
  const std::array<glm::vec3, 3> axes{glm::vec3{1, 0, 0}, glm::vec3{0, 1, 0}, glm::vec3{0, 0, 1}};
  const std::array<AxisView, 6> views{AxisView::Right,  AxisView::Left,  AxisView::Top,
                                      AxisView::Bottom, AxisView::Front, AxisView::Back};
  const ui::Theme &theme = ui::currentTheme();
  const std::array<ImU32, 3> colors{ui::toU32(theme.axisX), ui::toU32(theme.axisY), ui::toU32(theme.axisZ)};
  const std::array<ImU32, 3> highlights{ui::toU32(theme.axisXHighlight), ui::toU32(theme.axisYHighlight),
                                        ui::toU32(theme.axisZHighlight)};
  const std::array<const char *, 3> labels{"X", "Y", "Z"};
  const auto &camera = m_view.camera.camera();
  for (std::size_t i = 0; i < ends.size(); ++i) {
    const glm::vec3 direction = axes[i / 2] * (i % 2 == 0 ? 1.0f : -1.0f);
    ends[i] = {
        {center.x + arm * glm::dot(direction, camera.right()), center.y - arm * glm::dot(direction, camera.up())},
        -glm::dot(direction, camera.forward()),
        views[i],
        colors[i / 2],
        highlights[i / 2],
        labels[i / 2],
        i % 2 == 0};
  }
  std::ranges::sort(ends, {}, &End::depth);
  ImGui::SetCursorScreenPos({center.x - extent, center.y - extent});
  ImGui::InvisibleButton("##axis-navigation", {extent * 2.0f, extent * 2.0f});
  const bool hovered = ImGui::IsItemHovered();
  const bool active = ImGui::IsItemActive();
  const ImVec2 mouse = ImGui::GetMousePos();
  AxisView hit = AxisView::User;
  for (const auto &end : ends) {
    const float x = mouse.x - end.position.x;
    const float y = mouse.y - end.position.y;
    if (x * x + y * y <= radius * radius) {
      hit = end.view;
    }
  }
  if (ImGui::IsItemActivated()) {
    cancelTransition();
    m_widgetAxis = hit;
    m_widgetDragging = false;
    m_view.pivot = viewPivot();
    m_view.hasPivot = true;
  }
  if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
    m_widgetDragging = true;
    const ImVec2 delta = ImGui::GetIO().MouseDelta;
    m_view.camera.orbit({delta.x, delta.y}, m_view.pivot);
  }
  const bool released = ImGui::IsItemDeactivated();
  if (released && !m_widgetDragging && hovered && hit == m_widgetAxis && hit != AxisView::User) {
    if (m_view.camera.axisView() == hit) {
      oppositeView();
    } else {
      snapView(hit);
    }
  }
  ImDrawList *draw = ImGui::GetWindowDrawList();
  draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
  draw->AddCircleFilled(center, extent - unit * 0.25f,
                        ui::toU32(hovered || active ? theme.widgetDiscActive : theme.widgetDisc));
  for (const auto &end : ends) {
    if (end.positive) {
      draw->AddLine(center, end.position, end.color, unit * 0.15f);
    }
    const bool highlighted = hovered && hit == end.view;
    draw->AddCircleFilled(end.position, radius, end.positive ? end.color : ui::toU32(theme.widgetNegative));
    draw->AddCircle(end.position, radius, highlighted ? end.highlight : end.color, 0, highlighted ? 2.5f : 1.5f);
    if (end.positive) {
      const ImVec2 text = ImGui::CalcTextSize(end.label);
      draw->AddText({end.position.x - text.x * 0.5f, end.position.y - text.y * 0.5f}, ui::toU32(theme.widgetLabel),
                    end.label);
    }
  }
  draw->PopClipRect();
  if (hovered && hit != AxisView::User && !active) {
    ImGui::SetTooltip("Click to align; drag to orbit");
  }
  ImGui::SetCursorScreenPos({center.x - extent - unit * 1.8f, center.y - unit * 0.75f});
  const bool orthographic = camera.projectionKind == renderer::ProjectionKind::Orthographic;
  if (ImGui::Button(orthographic ? "O##projection" : "P##projection", {unit * 1.6f, unit * 1.6f})) {
    toggleProjection();
  }
  const bool projectionHovered = ImGui::IsItemHovered();
  const bool projectionActive = ImGui::IsItemActive() || ImGui::IsItemDeactivated();
  if (projectionHovered) {
    ImGui::SetTooltip("Toggle perspective / orthographic (Numpad 5)");
  }
  return hovered || active || released || projectionHovered || projectionActive;
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

void ViewportPanel::cancelTransition() {
  if (!m_view.camera.transitioning()) {
    return;
  }
  m_view.pivot = m_view.camera.transitionPivot();
  m_view.hasPivot = true;
  if (m_view.hasFocusTarget) {
    m_view.focusTarget = m_view.pivot;
  }
  m_view.focusDistance = glm::distance(m_view.camera.camera().position, m_view.pivot);
  m_view.camera.cancelTransition();
}

void ViewportPanel::animateFrom(const FlyCamera &from, glm::vec3 pivot) {
  m_view.camera.transitionFrom(from, pivot, viewPivot(), m_immediateViews ? 0.0f : m_transitionDuration);
}

void ViewportPanel::snapView(AxisView view) {
  cancelTransition();
  const FlyCamera from = m_view.camera;
  const glm::vec3 pivot = viewPivot();
  m_view.pivot = viewPivot();
  m_view.hasPivot = true;
  m_view.camera.snap(view, m_view.pivot);
  animateFrom(from, pivot);
}

void ViewportPanel::oppositeView() {
  const FlyCamera from = m_view.camera;
  const glm::vec3 pivot = m_view.camera.transitioning() ? m_view.camera.transitionPivot() : viewPivot();
  m_view.pivot = pivot;
  m_view.hasPivot = true;
  m_view.camera.opposite(pivot);
  animateFrom(from, pivot);
}
void ViewportPanel::toggleProjection() {
  cancelTransition();
  const FlyCamera from = m_view.camera;
  const glm::vec3 pivot = viewPivot();
  m_view.camera.toggleProjection(pivot);
  animateFrom(from, pivot);
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
  cancelTransition();
  const FlyCamera from = m_view.camera;
  const glm::vec3 pivot = viewPivot();
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
  animateFrom(from, pivot);
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
