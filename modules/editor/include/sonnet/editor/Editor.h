#pragma once

#include <sonnet/editor/LogPanel.h>
#include <sonnet/editor/PrimitiveScene.h>
#include <sonnet/editor/StatisticsPanel.h>
#include <sonnet/editor/ViewportPanel.h>

#include <sonnet/platform/Event.h>
#include <sonnet/platform/Platform.h>
#include <sonnet/platform/Window.h>
#include <sonnet/renderer/RenderGraph.h>
#include <sonnet/renderer/Renderer.h>
#include <sonnet/rhi/Device.h>
#include <sonnet/rhi/Swapchain.h>
#include <sonnet/ui/ImGuiLayer.h>

#include <optional>

union SDL_Event;

namespace sonnet::editor {

// The editor shell (docs/roadmap.md, M1): Dear ImGui docking layout with the viewport, log and
// statistics panels around the primitive scene. The application owns the window, device and
// swapchain and calls the steps below in its frame order (docs/architecture.md, "Application
// lifecycle").
class Editor {
public:
  // The swapchain only tells the ImGui pipeline its format and image count.
  Editor(platform::Platform &platform, platform::IWindow &window, rhi::IDevice &device,
         const rhi::ISwapchain &swapchain);
  ~Editor();
  Editor(const Editor &) = delete;
  Editor &operator=(const Editor &) = delete;

  // 1. Events, as the platform delivers them.
  void nativeEvent(const SDL_Event &event);
  void event(const platform::Event &event);
  // 2. Simulation and UI for this frame.
  void update(float dt);
  // 3. Recording: the scene into the viewport target, then the UI into the swapchain image when
  //    there is one.
  void render(rhi::ICommandList &commands, const std::optional<rhi::SwapchainImage> &swapchainImage);
  // 4. After the device's endFrame: the extra OS windows.
  void afterPresent();

  [[nodiscard]] bool quitRequested() const noexcept {
    return m_quitRequested;
  }

private:
  void drawMenuBar();
  void buildDefaultLayout(unsigned dockspace);

  platform::IWindow &m_window;
  rhi::IDevice &m_device;
  ui::ImGuiLayer m_imgui;
  renderer::Renderer m_renderer;
  renderer::RenderGraph m_graph;
  PrimitiveScene m_scene;
  LogPanel m_logPanel;
  StatisticsPanel m_statisticsPanel;
  ViewportPanel m_viewportPanel;
  renderer::SceneView m_view;

  glm::vec2 m_lookDelta{0.0f, 0.0f};
  bool m_layoutBuilt{false};
  bool m_showViewport{true};
  bool m_showLog{true};
  bool m_showStatistics{true};
  bool m_showOverlay{true};
  bool m_quitRequested{false};
  float m_frameMilliseconds{0.0f};
};

} // namespace sonnet::editor
