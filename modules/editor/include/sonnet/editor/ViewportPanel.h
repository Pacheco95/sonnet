#pragma once

#include <sonnet/editor/FlyCamera.h>
#include <sonnet/editor/StatisticsPanel.h>

#include <sonnet/core/Math.h>
#include <sonnet/renderer/RenderTarget.h>
#include <sonnet/rhi/Device.h>
#include <sonnet/ui/ImGuiLayer.h>

#include <imgui.h>

#include <functional>

namespace sonnet::editor {

// What happened over the viewport image this frame, in screen pixels, for the gizmo and picking.
struct ViewportInput {
  bool visible{false};
  bool hovered{false};
  bool focused{false};    // the viewport window has the keyboard focus
  glm::vec2 origin{0.0f}; // top-left of the image
  glm::vec2 size{0.0f};
  glm::vec2 mouse{0.0f};
  bool leftClicked{false};
  bool leftDoubleClicked{false};
  bool leftDown{false};
  ImDrawList *drawList{nullptr}; // the window's, valid inside the overlay callback
};

// The dockable scene view: owns the render target the scene is drawn into, displays it with
// ImGui::Image, and drives the fly camera while the right mouse button is held over it; the wheel over it dollies
// the camera when the right button is up. Dragging with the middle button orbits the camera around
// the pivot: the point the last focus framed, or else a point in front of the camera at the
// distance that focus used. The pivot is kept until the next focus or until WASD moves the camera.
class ViewportPanel {
public:
  ViewportPanel(rhi::IDevice &device, ui::ImGuiLayer &imgui);
  ~ViewportPanel();
  ViewportPanel(const ViewportPanel &) = delete;
  ViewportPanel &operator=(const ViewportPanel &) = delete;

  // Builds the window. lookDelta is this frame's relative mouse motion; the camera consumes it
  // only while it is active. `overlay` runs inside the window, over the image, for the gizmo;
  // `header` runs at the top of the window, above the image, for the scene tabs. Returns whether
  // the camera wants relative mouse mode.
  bool draw(bool &open, float dt, glm::vec2 lookDelta, StatisticsPanel *statistics,
            const std::function<void(const ViewportInput &)> &overlay = {}, const std::function<void()> &header = {});

  // While true the wheel belongs to the game (play mode with the viewport focused), so it does
  // not dolly the camera.
  void setWheelForGame(bool value) noexcept {
    m_wheelForGame = value;
  }

  // Turns the camera towards `target` from a distance that fits `radius`.
  void focus(glm::vec3 target, float radius);
  // The pixel of the render target under a screen position.
  [[nodiscard]] glm::uvec2 targetPixel(glm::vec2 screen) const;

  [[nodiscard]] renderer::RenderTarget &target() noexcept {
    return m_target;
  }
  [[nodiscard]] const renderer::RenderTarget &target() const noexcept {
    return m_target;
  }
  [[nodiscard]] const FlyCamera &camera() const noexcept {
    return m_camera;
  }
  [[nodiscard]] FlyCamera &camera() noexcept {
    return m_camera;
  }
  [[nodiscard]] bool cameraActive() const noexcept {
    return m_cameraActive;
  }
  [[nodiscard]] const ViewportInput &input() const noexcept {
    return m_input;
  }

private:
  void resizeTarget(glm::uvec2 size);

  ui::ImGuiLayer &m_imgui;
  renderer::RenderTarget m_target;
  ImTextureID m_texture{0};
  FlyCamera m_camera;
  ViewportInput m_input;
  bool m_cameraActive{false};
  bool m_wheelForGame{false};
  glm::vec3 m_focusTarget{0.0f};
  bool m_hasFocusTarget{false};
  bool m_orbiting{false}; // the active camera drag is the middle button's orbit
  glm::vec3 m_pivot{0.0f};
  bool m_hasPivot{false};
  float m_focusDistance{10.0f}; // how far the last focus stood back, for a pivot without an object
};

} // namespace sonnet::editor
