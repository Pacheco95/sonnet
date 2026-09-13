#pragma once

#include <sonnet/editor/FlyCamera.h>
#include <sonnet/editor/StatisticsPanel.h>

#include <sonnet/core/Math.h>
#include <sonnet/renderer/RenderTarget.h>
#include <sonnet/rhi/Device.h>
#include <sonnet/ui/ImGuiLayer.h>

#include <imgui.h>

namespace sonnet::editor {

// The dockable scene view: owns the render target the scene is drawn into, displays it with
// ImGui::Image, and drives the fly camera while the right mouse button is held over it.
class ViewportPanel {
public:
  ViewportPanel(rhi::IDevice &device, ui::ImGuiLayer &imgui);
  ~ViewportPanel();
  ViewportPanel(const ViewportPanel &) = delete;
  ViewportPanel &operator=(const ViewportPanel &) = delete;

  // Builds the window. lookDelta is this frame's relative mouse motion; the camera consumes it
  // only while it is active. Returns whether the camera wants relative mouse mode.
  bool draw(bool &open, float dt, glm::vec2 lookDelta, StatisticsPanel *overlay);

  [[nodiscard]] renderer::RenderTarget &target() noexcept {
    return m_target;
  }
  [[nodiscard]] const FlyCamera &camera() const noexcept {
    return m_camera;
  }
  [[nodiscard]] bool cameraActive() const noexcept {
    return m_cameraActive;
  }

private:
  void resizeTarget(glm::uvec2 size);

  ui::ImGuiLayer &m_imgui;
  renderer::RenderTarget m_target;
  ImTextureID m_texture{0};
  FlyCamera m_camera;
  bool m_cameraActive{false};
};

} // namespace sonnet::editor
