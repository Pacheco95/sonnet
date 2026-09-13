#pragma once

#include <sonnet/editor/CommandStack.h>
#include <sonnet/editor/Gizmo.h>
#include <sonnet/editor/HierarchyPanel.h>
#include <sonnet/editor/InspectorPanel.h>
#include <sonnet/editor/LogPanel.h>
#include <sonnet/editor/Preferences.h>
#include <sonnet/editor/Project.h>
#include <sonnet/editor/Selection.h>
#include <sonnet/editor/StatisticsPanel.h>
#include <sonnet/editor/ViewportPanel.h>

#include <sonnet/core/Error.h>
#include <sonnet/platform/Event.h>
#include <sonnet/platform/Platform.h>
#include <sonnet/platform/Window.h>
#include <sonnet/renderer/Picker.h>
#include <sonnet/renderer/RenderGraph.h>
#include <sonnet/renderer/Renderer.h>
#include <sonnet/rhi/Device.h>
#include <sonnet/rhi/Swapchain.h>
#include <sonnet/ui/ImGuiLayer.h>
#include <sonnet/world/DrawList.h>
#include <sonnet/world/World.h>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

union SDL_Event;

namespace sonnet::editor {

// The editor (docs/editor.md): the world being edited, the panels around the viewport, the
// undo history, play mode and the project. The application owns the window, device and
// swapchain and calls the steps below in its frame order (docs/architecture.md, "Application
// lifecycle").
class Editor {
public:
  // The swapchain only tells the ImGui pipeline its format and image count. `explorer` serves
  // the flecs explorer, for Debug builds of the application.
  Editor(platform::Platform &platform, platform::IWindow &window, rhi::IDevice &device,
         const rhi::ISwapchain &swapchain, bool explorer = false);
  ~Editor();
  Editor(const Editor &) = delete;
  Editor &operator=(const Editor &) = delete;

  // 1. Events, as the platform delivers them.
  void nativeEvent(const SDL_Event &event);
  void event(const platform::Event &event);
  // 2. The UI, then the world's frame: the panels edit the world, the systems run, the draw list
  //    is built.
  void update(float dt);
  // 3. Recording: the scene, ids and outline into the viewport target, then the UI into the
  //    swapchain image when there is one.
  void render(rhi::ICommandList &commands, const std::optional<rhi::SwapchainImage> &swapchainImage);
  // 4. After the device's endFrame: the extra OS windows.
  void afterPresent();

  // Projects and scenes. Opening a project loads its prefabs and start scene; a failure leaves
  // the current scene in place and is reported to the log.
  [[nodiscard]] core::Result<void> openProject(const std::filesystem::path &directory);
  [[nodiscard]] core::Result<void> createProject(const std::filesystem::path &directory, std::string name);
  [[nodiscard]] core::Result<void> openScene(const std::filesystem::path &file);
  [[nodiscard]] core::Result<void> saveScene();
  [[nodiscard]] core::Result<void> saveSceneAs(const std::filesystem::path &file);
  // The starter scene, unsaved.
  void newScene();

  // Play mode (docs/architecture.md, "Editor and player"): play snapshots the scene and enables
  // the simulation, stop restores the snapshot and drops the undo history.
  void play();
  void stop();
  [[nodiscard]] bool isPlaying() const noexcept {
    return m_world.isPlaying();
  }

  [[nodiscard]] world::World &world() noexcept {
    return m_world;
  }
  [[nodiscard]] Selection &selection() noexcept {
    return m_selection;
  }
  [[nodiscard]] CommandStack &commands() noexcept {
    return m_commands;
  }
  [[nodiscard]] const std::optional<Project> &project() const noexcept {
    return m_project;
  }
  [[nodiscard]] const std::filesystem::path &scenePath() const noexcept {
    return m_scenePath;
  }
  [[nodiscard]] bool isDirty() const noexcept {
    return m_commands.revision() != m_savedRevision;
  }
  [[nodiscard]] Gizmo &gizmo() noexcept {
    return m_gizmo;
  }
  [[nodiscard]] bool quitRequested() const noexcept {
    return m_quitRequested;
  }

private:
  enum class Modal : std::uint8_t {
    None,
    NewProject,
    OpenProject,
    SaveSceneAs,
  };

  void drawMenuBar();
  void drawModal();
  void handleShortcuts();
  void drawViewportOverlay(const ViewportInput &input);
  void buildDefaultLayout(unsigned dockspace);
  void applyPick(std::uint32_t id);
  void markSaved();
  void updateTitle();
  void loadPrefabs();
  void focusSelection();
  void openLocation(const std::string &path, int line);
  [[nodiscard]] core::Result<void> loadSceneFile(const std::filesystem::path &file);

  platform::IWindow &m_window;
  rhi::IDevice &m_device;
  ui::ImGuiLayer m_imgui;
  renderer::Renderer m_renderer;
  renderer::RenderGraph m_graph;
  renderer::Picker m_picker;
  world::World m_world;
  world::PrimitiveMeshes m_meshes;
  std::vector<renderer::DrawItem> m_draws;
  renderer::SceneView m_view;
  std::vector<std::uint32_t> m_outlineIds;

  Selection m_selection;
  CommandStack m_commands;
  Gizmo m_gizmo;
  std::filesystem::path m_preferencesFile;
  Preferences m_preferences;
  std::optional<Project> m_project;
  std::filesystem::path m_scenePath;
  std::uint64_t m_savedRevision{0};
  nlohmann::json m_snapshot;

  LogPanel m_logPanel;
  StatisticsPanel m_statisticsPanel;
  ViewportPanel m_viewportPanel;
  HierarchyPanel m_hierarchyPanel;
  InspectorPanel m_inspectorPanel;

  glm::vec2 m_lookDelta{0.0f, 0.0f};
  std::optional<Selection::Mode> m_pendingPickMode;
  Modal m_modal{Modal::None};
  std::string m_modalPath;
  std::string m_modalName;
  std::string m_modalError;
  std::string m_title;
  bool m_relativeMouseRequested{false};
  bool m_layoutBuilt{false};
  bool m_showViewport{true};
  bool m_showHierarchy{true};
  bool m_showInspector{true};
  bool m_showLog{true};
  bool m_showStatistics{true};
  bool m_showOverlay{true};
  bool m_quitRequested{false};
  float m_frameMilliseconds{0.0f};
};

} // namespace sonnet::editor
