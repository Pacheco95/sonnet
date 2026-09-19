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
  bool leftDown{false};
  ImDrawList *drawList{nullptr}; // the window's, valid inside the overlay callback
};

// The dockable scene view: owns the render target the scene is drawn into, displays it with
// ImGui::Image, and drives the fly camera while the right mouse button is held over it.
class ViewportPanel {
public:
  ViewportPanel(rhi::IDevice &device, ui::ImGuiLayer &imgui);
  ~ViewportPanel();
  ViewportPanel(const ViewportPanel &) = delete;
  ViewportPanel &operator=(const ViewportPanel &) = delete;

  // Builds the window. lookDelta is this frame's relative mouse motion; the camera consumes it
  // only while it is active. `overlay` runs inside the window, over the image, for the gizmo.
  // Returns whether the camera wants relative mouse mode.
  bool draw(bool &open, float dt, glm::vec2 lookDelta, StatisticsPanel *statistics,
            const std::function<void(const ViewportInput &)> &overlay = {});

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
};

} // namespace sonnet::editor
