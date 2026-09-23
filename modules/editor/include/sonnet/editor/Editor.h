#pragma once

#include <sonnet/editor/AssetBrowserPanel.h>
#include <sonnet/editor/CommandStack.h>
#include <sonnet/editor/Export.h>
#include <sonnet/editor/Gizmo.h>
#include <sonnet/editor/HierarchyPanel.h>
#include <sonnet/editor/InspectorPanel.h>
#include <sonnet/editor/LogPanel.h>
#include <sonnet/editor/Preferences.h>
#include <sonnet/editor/Project.h>
#include <sonnet/editor/Selection.h>
#include <sonnet/editor/StatisticsPanel.h>
#include <sonnet/editor/ViewportPanel.h>

#include <sonnet/assets/AssetDatabase.h>
#include <sonnet/assets/ShaderCompiler.h>
#include <sonnet/audio/AudioDevice.h>
#include <sonnet/core/Error.h>
#include <sonnet/core/JobSystem.h>
#include <sonnet/physics/PhysicsWorld.h>
#include <sonnet/platform/Event.h>
#include <sonnet/platform/InputState.h>
#include <sonnet/platform/Platform.h>
#include <sonnet/platform/Window.h>
#include <sonnet/renderer/Picker.h>
#include <sonnet/renderer/RenderGraph.h>
#include <sonnet/renderer/Renderer.h>
#include <sonnet/rhi/Device.h>
#include <sonnet/rhi/Swapchain.h>
#include <sonnet/scripting/ScriptRuntime.h>
#include <sonnet/ui/ImGuiLayer.h>
#include <sonnet/world/Animation.h>
#include <sonnet/world/DrawList.h>
#include <sonnet/world/World.h>

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
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

  // Cooks the open project and assembles a runnable directory beside the bundle
  // (docs/editor.md, "Export"). The dialog is this with the fields it collected.
  [[nodiscard]] core::Result<ExportReport> exportProject(const ExportOptions &options);

  // Compiles every engine shader from the checkout's sources and rebuilds its pipelines: what
  // the Tools menu does, and what the source poll does for one changed file (docs/editor.md,
  // "Shader hot reload"). Fails when the sources are not found or a shader does not compile,
  // in which case its pipelines stay as they were.
  [[nodiscard]] core::Result<void> reloadShaders();

  // Play mode (docs/architecture.md, "Editor and player"): play snapshots the scene and enables
  // the simulation, physics, scripts, animation and sound; stop restores the snapshot, drops the
  // undo history and the scripts' state, and silences everything.
  void play();
  void stop();
  [[nodiscard]] bool isPlaying() const noexcept {
    return m_world.isPlaying();
  }

  [[nodiscard]] world::World &world() noexcept {
    return m_world;
  }
  [[nodiscard]] physics::IPhysicsWorld &physics() noexcept {
    return *m_physics;
  }
  [[nodiscard]] scripting::IScriptRuntime &scripts() noexcept {
    return *m_scripts;
  }
  [[nodiscard]] audio::IAudioDevice &audio() noexcept {
    return *m_audio;
  }
  // What the game's scripts see: fed while playing with the viewport focused (docs/editor.md,
  // "Play mode").
  [[nodiscard]] const platform::InputState &gameInput() const noexcept {
    return m_input;
  }
  // The physics colliders' outlines over the scene, from the View menu.
  void setShowColliders(bool show) noexcept {
    m_showColliders = show;
  }
  // The term of the forward shading the viewport shows, from View > Shading term.
  void setShadingTerm(renderer::DebugView view);

  // Screenshots (docs/editor.md, "Screenshots"). The next frame that can copies the viewport's
  // scene into `viewport` and the whole window into `window` as PNG files; an empty path skips
  // that one. It is written in afterPresent, once the frame has finished on the GPU, and the
  // outcome is taken once with takeScreenshotResult. The window needs a readable swapchain.
  void requestScreenshots(std::filesystem::path viewport, std::filesystem::path window);
  [[nodiscard]] std::optional<core::Result<void>> takeScreenshotResult();
  [[nodiscard]] assets::AssetDatabase &assets() noexcept {
    return m_assets;
  }
  [[nodiscard]] AssetBrowserPanel &assetBrowser() noexcept {
    return m_assetBrowserPanel;
  }
  [[nodiscard]] Selection &selection() noexcept {
    return m_selection;
  }
  [[nodiscard]] CommandStack &commands() noexcept {
    return m_commands;
  }
  [[nodiscard]] const std::optional<assets::Project> &project() const noexcept {
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
  [[nodiscard]] const ViewportPanel &viewport() const noexcept {
    return m_viewportPanel;
  }
  [[nodiscard]] bool quitRequested() const noexcept {
    return m_quitRequested;
  }
  // The pick ids the last update queued for the outline: the selection and its descendants.
  [[nodiscard]] std::span<const std::uint32_t> outlineIds() const noexcept {
    return m_outlineIds;
  }

private:
  // A screenshot's copy, waiting in a host-visible buffer for its frame to finish.
  struct Readback {
    std::filesystem::path file;
    rhi::BufferHandle buffer;
    glm::uvec2 size;
    rhi::Format format;
  };

  enum class Modal : std::uint8_t {
    None,
    NewProject,
    OpenProject,
    SaveSceneAs,
    Export,
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
  [[nodiscard]] std::optional<std::filesystem::path> shaderSourceDirectory();
  [[nodiscard]] core::Result<void> reloadShader(std::string_view name);
  void pollShaders();
  // Declares a pass copying `image` into a new readback buffer for `file`.
  void addReadback(renderer::GraphImage image, glm::uvec2 size, rhi::Format format, std::filesystem::path file);
  void writeReadbacks();
  // Whether game input goes to the scripts: playing, with the viewport focused and the camera idle.
  [[nodiscard]] bool gameInputActive() const;

  platform::IWindow &m_window;
  rhi::IDevice &m_device;
  const rhi::ISwapchain &m_swapchain;
  std::filesystem::path m_basePath;
  // First of the members that outlive work, so everything scheduling onto it is destroyed before
  // it is (ADR-0013).
  core::JobSystem m_jobs;
  ui::ImGuiLayer m_imgui;
  renderer::Renderer m_renderer;
  renderer::RenderGraph m_graph;
  renderer::Picker m_picker;
  assets::AssetDatabase m_assets;
  world::World m_world;
  // After the world, which they register into and have to be destroyed before; scripts after
  // physics so their fixed update follows the physics step (ADR-0009).
  platform::InputState m_input;
  std::unique_ptr<physics::IPhysicsWorld> m_physics;
  std::unique_ptr<scripting::IScriptRuntime> m_scripts;
  // After the scripts, so a pose a script sets this frame is sampled over; the audio device, after
  // everything that moves entities, hears them where they end up.
  world::AnimationSystem m_animation;
  std::unique_ptr<audio::IAudioDevice> m_audio;
  std::vector<renderer::DrawItem> m_draws;
  std::vector<glm::mat4> m_joints;
  std::vector<renderer::DebugLine> m_debugLines;
  std::vector<renderer::Light> m_lights;
  renderer::SceneView m_view;
  std::vector<std::uint32_t> m_outlineIds;
  std::optional<std::pair<std::filesystem::path, std::filesystem::path>> m_screenshotRequest; // viewport, window
  std::vector<Readback> m_readbacks;
  std::optional<core::Result<void>> m_screenshotResult;

  Selection m_selection;
  CommandStack m_commands;
  Gizmo m_gizmo;
  std::filesystem::path m_preferencesFile;
  Preferences m_preferences;
  std::optional<assets::Project> m_project;
  std::filesystem::path m_scenePath;
  std::uint64_t m_savedRevision{0};
  nlohmann::json m_snapshot;
  std::unique_ptr<assets::ShaderCompiler> m_shaderCompiler;
  std::filesystem::path m_shaderSources;
  std::unordered_map<std::string, std::filesystem::file_time_type> m_shaderTimes;
  std::chrono::steady_clock::time_point m_lastShaderPoll{};
  bool m_shaderPollDisabled{false};

  LogPanel m_logPanel;
  StatisticsPanel m_statisticsPanel;
  ViewportPanel m_viewportPanel;
  HierarchyPanel m_hierarchyPanel;
  InspectorPanel m_inspectorPanel;
  AssetBrowserPanel m_assetBrowserPanel;

  glm::vec2 m_lookDelta{0.0f, 0.0f};
  std::optional<Selection::Mode> m_pendingPickMode;
  Modal m_modal{Modal::None};
  std::string m_modalPath;
  std::string m_modalName;
  std::string m_modalError;
  std::string m_modalMessage; // what the last export did, shown in its dialog
  assets::CookPlatform m_exportPlatform{assets::hostPlatform()};
  std::string m_title;
  bool m_relativeMouseRequested{false};
  bool m_layoutBuilt{false};
  bool m_showViewport{true};
  bool m_showHierarchy{true};
  bool m_showInspector{true};
  bool m_showLog{true};
  bool m_showAssets{true};
  bool m_showStatistics{true};
  bool m_showOverlay{true};
  bool m_showColliders{false};
  bool m_gameInputWasActive{false};
  glm::vec2 m_mainViewportOrigin{0.0f, 0.0f};
  bool m_quitRequested{false};
  float m_frameMilliseconds{0.0f};
};

} // namespace sonnet::editor
