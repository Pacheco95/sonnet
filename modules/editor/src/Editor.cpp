#include <sonnet/editor/Editor.h>

#include <sonnet/core/Log.h>
#include <sonnet/core/Profile.h>
#include <sonnet/core/Version.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <variant>

namespace sonnet::editor {

Editor::Editor(platform::Platform &platform, platform::IWindow &window, rhi::IDevice &device,
               const rhi::ISwapchain &swapchain)
    : m_window(window), m_device(device), m_imgui({.window = &window,
                                                   .device = &device,
                                                   .swapchainFormat = swapchain.format(),
                                                   .swapchainImageCount = swapchain.imageCount(),
                                                   .docking = true,
                                                   // Platform windows need a display; the headless driver has none.
                                                   .viewports = !platform.isHeadless()}),
      m_renderer(device, platform.basePath() / "shaders"), m_graph(device), m_scene(m_renderer),
      m_viewportPanel(device, m_imgui) {
  m_view.light = m_scene.light();
  SONNET_LOG_INFO("editor ready");
}

Editor::~Editor() {
  m_device.waitIdle();
}

void Editor::nativeEvent(const SDL_Event &event) {
  m_imgui.processEvent(event);
}

void Editor::event(const platform::Event &event) {
  if (const auto *moved = std::get_if<platform::MouseMoved>(&event)) {
    m_lookDelta += moved->delta;
  }
}

void Editor::update(float dt) {
  SONNET_ZONE();
  m_frameMilliseconds = dt * 1000.0f;
  m_scene.update(dt);

  m_imgui.beginFrame();
  const ImGuiID dockspace = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
  if (!m_layoutBuilt) {
    buildDefaultLayout(dockspace);
    m_layoutBuilt = true;
  }
  drawMenuBar();

  if (m_showViewport) {
    const bool wantsRelativeMouse =
        m_viewportPanel.draw(m_showViewport, dt, m_lookDelta, m_showOverlay ? &m_statisticsPanel : nullptr);
    if (wantsRelativeMouse != m_window.relativeMouseMode()) {
      m_window.setRelativeMouseMode(wantsRelativeMouse);
    }
  }
  m_lookDelta = {0.0f, 0.0f};
  if (m_showLog) {
    m_logPanel.draw(m_showLog);
  }
  if (m_showStatistics) {
    m_statisticsPanel.drawWindow(m_showStatistics);
  }
  m_imgui.endFrame();

  m_view.camera = m_viewportPanel.camera().camera();
  m_view.draws = m_scene.draws();
}

void Editor::drawMenuBar() {
  if (!ImGui::BeginMainMenuBar()) {
    return;
  }
  if (ImGui::BeginMenu("File")) {
    if (ImGui::MenuItem("Quit", "Ctrl+Q")) {
      m_quitRequested = true;
    }
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("View")) {
    ImGui::MenuItem("Viewport", nullptr, &m_showViewport);
    ImGui::MenuItem("Log", nullptr, &m_showLog);
    ImGui::MenuItem("Statistics", nullptr, &m_showStatistics);
    ImGui::MenuItem("Statistics overlay", nullptr, &m_showOverlay);
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Help")) {
    ImGui::MenuItem(("Sonnet " + core::engineVersion().toString()).c_str(), nullptr, false, false);
    ImGui::EndMenu();
  }
  ImGui::EndMainMenuBar();
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Q)) {
    m_quitRequested = true;
  }
}

void Editor::buildDefaultLayout(unsigned dockspace) {
  // Viewport in the centre, log along the bottom, statistics down the right side.
  ImGui::DockBuilderRemoveNode(dockspace);
  ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dockspace, ImGui::GetMainViewport()->WorkSize);
  ImGuiID centre = dockspace;
  const ImGuiID bottom = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down, 0.25f, nullptr, &centre);
  const ImGuiID right = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, 0.22f, nullptr, &centre);
  ImGui::DockBuilderDockWindow("Viewport", centre);
  ImGui::DockBuilderDockWindow("Log", bottom);
  ImGui::DockBuilderDockWindow("Statistics", right);
  ImGui::DockBuilderFinish(dockspace);
}

void Editor::render(rhi::ICommandList &commands, const std::optional<rhi::SwapchainImage> &swapchainImage) {
  SONNET_ZONE();
  m_graph.reset();
  renderer::RenderTarget &target = m_viewportPanel.target();
  renderer::GraphImage sceneColor;
  if (target.isValid()) {
    sceneColor = m_graph.importImage(target.color());
    m_renderer.addScenePasses(m_graph, m_view, sceneColor, m_graph.importImage(target.depth()));
  }
  if (swapchainImage) {
    const renderer::GraphImage backbuffer = m_graph.importImage(swapchainImage->image, rhi::ImageLayout::Present);
    m_graph.addPass(
        "imgui",
        [&](renderer::PassBuilder &builder) {
          builder.color(backbuffer, rhi::LoadOp::Clear, {0.1f, 0.1f, 0.12f, 1.0f});
          if (sceneColor.isValid()) {
            builder.sample(sceneColor);
          }
        },
        [this](rhi::ICommandList &cmd, const renderer::PassResources &) { m_imgui.draw(cmd); });
  }
  m_graph.execute(commands);

  m_statisticsPanel.record({.frameMilliseconds = m_frameMilliseconds,
                            .graph = &m_graph.statistics(),
                            .renderer = m_renderer.statistics(),
                            .memory = m_device.memoryBudget()});
}

void Editor::afterPresent() {
  m_imgui.renderPlatformWindows();
}

} // namespace sonnet::editor
