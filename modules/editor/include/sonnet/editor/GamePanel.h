#pragma once

#include <sonnet/editor/ViewportPanel.h>

#include <sonnet/renderer/RenderTarget.h>
#include <sonnet/rhi/Device.h>
#include <sonnet/ui/ImGuiLayer.h>

namespace sonnet::editor {

// The dockable game view: a render target of its own, sized to the panel, that the editor draws
// through the scene's Camera entity, and displays with ImGui::Image. It has no camera of its own,
// no gizmo and no picking; while playing, what the mouse does over its image is the game's input
// (docs/editor.md, "Play mode"). The target exists only while the panel is on screen, so a closed
// or hidden panel costs the renderer no second view (ADR-0021).
class GamePanel {
public:
  GamePanel(rhi::IDevice &device, ui::ImGuiLayer &imgui);
  ~GamePanel();
  GamePanel(const GamePanel &) = delete;
  GamePanel &operator=(const GamePanel &) = delete;

  void draw(bool &open);
  // The panel is closed this frame: releases the target and forgets the last frame's input.
  void hide();

  [[nodiscard]] renderer::RenderTarget &target() noexcept {
    return m_target;
  }
  [[nodiscard]] const renderer::RenderTarget &target() const noexcept {
    return m_target;
  }
  [[nodiscard]] const ViewportInput &input() const noexcept {
    return m_input;
  }

private:
  void resizeTarget(glm::uvec2 size);

  ui::ImGuiLayer &m_imgui;
  renderer::RenderTarget m_target;
  ImTextureID m_texture{0};
  ViewportInput m_input;
};

} // namespace sonnet::editor
