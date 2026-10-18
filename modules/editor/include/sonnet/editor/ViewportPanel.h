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
// the camera when the right button is up. Dragging with the middle button pans the camera in its
// plane; Alt with the left button orbits the camera around the pivot: the point the last focus framed, or else a point in front of the camera at the
// distance that focus used. The pivot is kept until the next focus or until WASD moves the camera.
class ViewportPanel {
public:
  // Everything about where the viewport looks from; a scene tab keeps its own.
  struct View {
    FlyCamera camera;
    glm::vec3 focusTarget{0.0f};
    bool hasFocusTarget{false};
    glm::vec3 pivot{0.0f};
    bool hasPivot{false};
    float focusDistance{10.0f}; // how far the last focus stood back, for a pivot without an object
  };

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
  void setTransitionDuration(float seconds) noexcept {
    m_transitionDuration = seconds > 0.0f ? seconds : 0.0f;
    if (m_transitionDuration == 0.0f) {
      m_view.camera.finishTransition();
    }
  }
  void setImmediateViews(bool immediate) {
    m_immediateViews = immediate;
    if (immediate) {
      m_view.camera.finishTransition();
    }
  }

  void snapView(AxisView view);
  void oppositeView();
  void toggleProjection();
  void drawViewMenu();
  // The pixel of the render target under a screen position.
  [[nodiscard]] glm::uvec2 targetPixel(glm::vec2 screen) const;

  [[nodiscard]] renderer::RenderTarget &target() noexcept {
    return m_target;
  }
  [[nodiscard]] const renderer::RenderTarget &target() const noexcept {
    return m_target;
  }
  [[nodiscard]] const FlyCamera &camera() const noexcept {
    return m_view.camera;
  }
  [[nodiscard]] FlyCamera &camera() noexcept {
    return m_view.camera;
  }
  [[nodiscard]] const View &view() const noexcept {
    return m_view;
  }
  void setView(const View &view) {
    m_view = view;
  }
  [[nodiscard]] bool cameraActive() const noexcept {
    return m_cameraActive;
  }
  [[nodiscard]] const ViewportInput &input() const noexcept {
    return m_input;
  }

private:
  void resizeTarget(glm::uvec2 size);
  void animateFrom(const FlyCamera &from, glm::vec3 pivot);
  void cancelTransition();
  bool drawAxisWidget(ImVec2 origin, ImVec2 size);
  [[nodiscard]] glm::vec3 viewPivot() const;

  ui::ImGuiLayer &m_imgui;
  renderer::RenderTarget m_target;
  ImTextureID m_texture{0};
  View m_view;
  ViewportInput m_input;
  AxisView m_widgetAxis{AxisView::User};
  bool m_widgetDragging{false};
  float m_transitionDuration{0.2f};
  bool m_immediateViews{false};
  bool m_cameraActive{false};
  bool m_wheelForGame{false};
  enum class Drag { Fly, Pan, Orbit };
  Drag m_drag{Drag::Fly}; // what the active camera drag does, from the buttons held this frame
  bool m_altOrbit{false}; // a left press with Alt held started the drag; it lasts while the button does
};

} // namespace sonnet::editor
