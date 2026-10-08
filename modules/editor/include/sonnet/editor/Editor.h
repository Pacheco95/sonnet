#pragma once

#include <sonnet/editor/AssetBrowserPanel.h>
#include <sonnet/editor/CommandStack.h>
#include <sonnet/editor/Export.h>
#include <sonnet/editor/FileDialog.h>
#include <sonnet/editor/GamePanel.h>
#include <sonnet/editor/Gizmo.h>
#include <sonnet/editor/HierarchyPanel.h>
#include <sonnet/editor/InspectorPanel.h>
#include <sonnet/editor/Layout.h>
#include <sonnet/editor/LogPanel.h>
#include <sonnet/editor/Preferences.h>
#include <sonnet/editor/Project.h>
#include <sonnet/editor/Recovery.h>
#include <sonnet/editor/Selection.h>
#include <sonnet/editor/ShaderCompiler.h>
#include <sonnet/editor/StatisticsPanel.h>
#include <sonnet/editor/ViewportPanel.h>
#include <sonnet/editor/WindowOrigins.h>

#include <sonnet/assets/AssetDatabase.h>
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
#include <sonnet/runtime/Screenshot.h>
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
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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
  // What an editor started without a project folder does: opens the most recent project when
  // the preferences ask for it. A project that cannot be opened is dropped from the recent list
  // (so the next start is quiet), logs a warning and leaves the starter scene.
  void reopenLastProject();
  [[nodiscard]] core::Result<void> createProject(const std::filesystem::path &directory, std::string name);
  // Opens the scene in a tab of its own, or switches to the tab that already has it. A failure
  // leaves the current tab as it was.
  [[nodiscard]] core::Result<void> openScene(const std::filesystem::path &file);
  [[nodiscard]] core::Result<void> saveScene();
  [[nodiscard]] core::Result<void> saveSceneAs(const std::filesystem::path &file);
  // The starter scene, unsaved, in a new tab.
  void newScene();

  // File dialogs (docs/editor.md, "File dialogs"). The menu and the modals' Browse buttons are
  // these calls. Results arrive from the operating system's chooser on another thread and are
  // applied by `update`; a cancel changes nothing, and an error or a headless run leaves the typed
  // path field as the way in.
  //
  // Replaces the chooser's backend, for tests. Drops a dialog in flight.
  void setFileDialogBackend(std::unique_ptr<IFileDialogBackend> backend);
  // File > Open scene...: a chooser for a `.scene.json`, opened like the asset browser opens one.
  void chooseSceneToOpen();
  // The Browse button of the open Path modal: a folder chooser, or a save chooser for Save scene as,
  // starting at the field's value. Fills the field with the pick; the user still confirms.
  void browseModalPath();
  // Save scene as...: the save chooser, or the path modal where there is no chooser.
  void chooseSceneToSave();
  // Ctrl+S and File > Save scene: saves to the scene's file, or asks for one when it has none.
  void saveSceneOrChoose();
  [[nodiscard]] const std::string &modalPath() const noexcept {
    return m_modalPath;
  }
  [[nodiscard]] const std::string &modalError() const noexcept {
    return m_modalError;
  }
  // Whether a Path modal is showing (New project, Open project, Save scene as or Export).
  [[nodiscard]] bool pathModalOpen() const noexcept;
  // File > Open project...
  void showOpenProjectModal();

  // The open scenes (docs/editor.md, "Scene tabs"). Switching or closing while playing stops play
  // mode first. Closing here discards unsaved changes; the tab's close button asks first.
  [[nodiscard]] std::size_t tabCount() const noexcept {
    return m_tabs.size();
  }
  [[nodiscard]] std::size_t activeTab() const noexcept {
    return m_activeTab;
  }
  [[nodiscard]] std::string tabTitle(std::size_t index) const;
  [[nodiscard]] bool tabDirty(std::size_t index) const;
  void switchToTab(std::size_t index);
  void closeTab(std::size_t index);

  // Quitting (docs/editor.md, "Quitting"). Every quit path asks here: with nothing unsaved the
  // editor quits at once, otherwise a dialog lists the dirty scenes and waits. A request while the
  // dialog is open changes nothing. The dialog's three buttons are the calls below.
  void requestQuit();
  [[nodiscard]] bool quitPromptOpen() const noexcept {
    return m_modal == Modal::Quit;
  }
  // The tabs with unsaved changes, in tab order.
  [[nodiscard]] std::vector<std::size_t> dirtyTabs() const;
  // Saves every dirty scene, then quits. A scene that cannot be saved (a failed write, or none has
  // a file yet) stops it: the editor stays open, the reason is shown, and the tab the user was in
  // is active again unless the failing scene has no file, which stays in front.
  void saveAllAndQuit();
  void discardAndQuit();
  void cancelQuit();

  // Crash recovery (docs/editor.md, "Crash recovery"). `root` is the per-user recovery directory,
  // under which each project has a folder of its own. Setting it starts the session for the open
  // project, and opening another project starts that project's; an editor without one (a test's, a
  // capture run) keeps no recovery files. A recovery set the last session left behind opens the
  // dialog whose three buttons are the calls below, and autosave waits for the answer.
  void setRecoveryDirectory(const std::filesystem::path &root);
  [[nodiscard]] const Recovery *recovery() const noexcept {
    return m_recovery.get();
  }
  [[nodiscard]] bool recoveryPromptOpen() const noexcept {
    return m_recovery && !m_recovery->decided();
  }
  // The titles of the scenes on offer, in the order they are restored.
  [[nodiscard]] std::vector<std::string> recoverableScenes() const;
  // Restore: each scene opens in a tab of its own, unsaved. One that does not load is quarantined
  // and the rest still open. The set stays on disk, guarded by the marker, until the restored
  // scenes have run for a while.
  void restoreRecovered();
  // Discard: deletes the set.
  void discardRecovered();
  // Open without restoring: leaves the set on disk, to be offered at the next start.
  void keepRecovered();
  // Writes every unsaved scene to the recovery directory now, what happens every 30 seconds and
  // when the window loses focus. Nothing while playing, or before the restore decision.
  void autosaveNow();

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
  // edits made meanwhile and the scripts' state, and silences everything.
  void play();
  void stop();
  // The Stop button's path (docs/editor.md, "Play mode"): stops at once when nothing was edited
  // while playing, and otherwise opens a dialog listing the edits that waits for one of the four
  // calls below. Every UI route that would stop play, which are switching or closing the scene's
  // tab, opening a scene or a project and quitting, asks the same way and goes on afterwards.
  void requestStop();
  [[nodiscard]] bool stopPromptOpen() const noexcept {
    return m_modal == Modal::Stop;
  }
  // The descriptions of the edits made since play, oldest first: what the dialog lists. The
  // simulation's own changes are not among them, and neither are asset edits.
  [[nodiscard]] std::vector<std::string> playChanges() const;
  // Stops, then applies the listed edits to the restored scene in order, putting them on the undo
  // history; the scene is then unsaved. An edit whose entity the restored scene lacks (a script
  // spawned it) is skipped and counted in skippedChanges.
  void keepPlayChanges();
  // Stops and writes the restored scene with the edits applied to `file`, leaving the open scene
  // as it was before play. A failure (an unwritable path, the open scene's own file) leaves
  // play running and is reported in modalError.
  [[nodiscard]] core::Result<void> savePlayChangesAs(const std::filesystem::path &file);
  // Stops and drops the edits: what stop does.
  void discardPlayChanges();
  // Closes the dialog and plays on.
  void cancelStop();
  // How many edits the last Keep or Save as could not apply.
  [[nodiscard]] std::size_t skippedChanges() const noexcept {
    return m_skippedChanges;
  }
  // Pause freezes a playing scene (docs/editor.md, "Play mode"): no fixed steps, physics, scripts
  // or animation, and the audio holds; edits still land on the frozen scene. Resume carries on
  // from the same state, and stop from a pause restores the snapshot as it does from play.
  // Both do nothing outside their state.
  void pause();
  void resume();
  [[nodiscard]] bool isPaused() const noexcept {
    return m_world.isPaused();
  }
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
  // The panel layout lives in `file` (an ImGui ini): read on the first frame, written every few
  // seconds after a change and when the editor is destroyed; it carries the shown panels and the
  // overlays too. A missing or unsplit layout gets the default one. Call before the first `update`;
  // the desktop editor sets it to `layout.ini` beside the preferences, a headless editor (a test's)
  // keeps none unless told.
  void setLayoutFile(const std::filesystem::path &file);
  // View > Reset layout: the Default layout (panels, overlays and arrangement), from the next frame.
  void resetLayout();
  // View > Layouts (docs/editor.md, "Layout presets"): a built-in layout or a saved one, applied
  // before the next frame. A missing or corrupt preset is skipped with a warning.
  void applyLayout(std::string_view name);
  // Saves the current panels, overlays and arrangement as `name`, sanitized to a file name. Refuses
  // a built-in's name, and an existing preset's unless `overwrite`.
  [[nodiscard]] core::Result<void> saveLayout(std::string_view name, bool overwrite = false);
  [[nodiscard]] core::Result<void> deleteLayout(std::string_view name);
  [[nodiscard]] std::vector<std::string> savedLayouts() const;
  [[nodiscard]] const ViewState &viewState() const noexcept {
    return m_viewState;
  }
  // Where the saved presets live; setLayoutFile puts it in a layouts directory beside the file.
  [[nodiscard]] const std::filesystem::path &layoutsDirectory() const noexcept {
    return m_layoutsDirectory;
  }
  // The physics colliders' outlines over the scene, from the View menu.
  void setShowColliders(bool show) noexcept {
    m_viewState.colliders = show;
  }
  // Wireframe cones for the spot lights in edit mode, from the View menu.
  void setShowLightGizmos(bool show) noexcept {
    m_viewState.lightGizmos = show;
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
  // Turns the viewport's camera towards the primary selected entity, from a distance that fits the
  // bounds of the meshes under it (F, a double-click in the hierarchy or the viewport).
  void focusSelection();
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
    // Playing, the stack holds only the play's own edits; what came before is m_dirtyBeforePlay.
    return (isPlaying() && m_dirtyBeforePlay) || !m_commands.isSaved();
  }
  [[nodiscard]] Gizmo &gizmo() noexcept {
    return m_gizmo;
  }
  [[nodiscard]] const ViewportPanel &viewport() const noexcept {
    return m_viewportPanel;
  }
  [[nodiscard]] ViewportPanel &viewport() noexcept {
    return m_viewportPanel;
  }
  [[nodiscard]] const GamePanel &gamePanel() const noexcept {
    return m_gamePanel;
  }
  [[nodiscard]] GamePanel &gamePanel() noexcept {
    return m_gamePanel;
  }
  // What scripts see of the game this frame: the camera they unproject through and the size of
  // the image it covers.
  [[nodiscard]] const scripting::ScriptView &scriptView() const noexcept {
    return m_scriptView;
  }
  void setShowGame(bool show) noexcept {
    m_viewState.game = show;
  }
  [[nodiscard]] const renderer::Renderer &renderer() const noexcept {
    return m_renderer;
  }
  [[nodiscard]] bool quitRequested() const noexcept {
    return m_quitRequested;
  }
  // The pick ids the last update queued for the outline: the selection and its descendants.
  [[nodiscard]] std::span<const std::uint32_t> outlineIds() const noexcept {
    return m_outlineIds;
  }

private:
  enum class Modal : std::uint8_t {
    None,
    NewProject,
    OpenProject,
    SaveSceneAs,
    Export,
    CloseTab,
    Quit,
    Stop,
    Recover,
    SaveLayout,
  };

  enum class DialogTarget : std::uint8_t {
    None,
    ModalPath,
    OpenScene,
    SaveScene,
  };

  // An open scene. The active tab's state lives in the editor's own members (the world, the
  // undo history, the selection, the path); the others hold theirs here, the world as scene JSON,
  // which is what play mode's snapshot round-trips too.
  struct SceneTab {
    std::uint64_t id{0};
    std::filesystem::path path;
    nlohmann::json content;
    CommandStack commands;
    Selection selection;
  };

  void drawMenuBar();
  void drawModal();
  void drawCloseTabModal();
  void drawQuitModal();
  void drawStopModal();
  void drawRecoverModal();
  void beginRecovery();
  void endRecovery();
  void updateRecovery();
  void writeRecovery();
  [[nodiscard]] bool loadRecovered(const Recovery::Entry &entry);
  void drawTabBar();
  void requestCloseTab(std::size_t index);
  void stashActiveTab();
  // Runs `action` now, or once the stop dialog has been answered when play holds edits to decide on.
  void afterStopPrompt(std::function<void()> action);
  [[nodiscard]] bool hasPlayChanges() const;
  void answerStop();
  void closeStopPrompt();
  enum class PlayEnd : std::uint8_t {
    Discard,
    Keep,
    SaveAs
  };
  [[nodiscard]] core::Result<void> endPlay(PlayEnd end, const std::filesystem::path &file);
  void restoreTab(std::size_t index);
  void addTab();
  void handleShortcuts();
  void refreshWindowOrigins();
  void startFileDialog(DialogTarget target, const FileDialogRequest &request);
  void pollFileDialog();
  void drawViewportOverlay(const ViewportInput &input);
  void buildLayout(unsigned dockspace, BuiltinLayout layout);
  void applyPendingLayout();
  void drawSaveLayoutModal();
  void applyPick(std::uint32_t id);
  void markSaved();
  void updateTitle();
  void loadPrefabs();
  void openLocation(const std::string &path, int line);
  [[nodiscard]] core::Result<void> loadSceneFile(const std::filesystem::path &file);
  [[nodiscard]] std::optional<std::filesystem::path> shaderSourceDirectory();
  [[nodiscard]] core::Result<void> reloadShader(std::string_view name);
  void pollShaders();

  // Whether game input goes to the scripts: playing, with the viewport focused and the camera idle.
  [[nodiscard]] bool gameInputActive() const;
  // Whether the game is seen through the Game panel: playing with it on screen. Scripts, the
  // audio listener and the game's input then come from it, and from the viewport otherwise.
  [[nodiscard]] bool gameViewActive() const;
  // The panel whose image the game's input is relative to.
  [[nodiscard]] const ViewportInput &gameInputSource() const;

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
  // The viewport's camera and size, for scripts turning a pointer into a ray.
  scripting::ScriptView m_scriptView;
  std::unique_ptr<physics::IPhysicsWorld> m_physics;
  world::AnimationSystem m_animation; // before the scripts, which hear its events the frame they happen
  std::unique_ptr<scripting::IScriptRuntime> m_scripts;
  // After the scripts, so a pose a script sets this frame is sampled over; the audio device, after
  // everything that moves entities, hears them where they end up.
  std::unique_ptr<audio::IAudioDevice> m_audio;
  std::vector<renderer::DrawItem> m_draws;
  std::vector<glm::mat4> m_joints;
  std::vector<float> m_morphWeights;
  std::vector<renderer::DebugLine> m_debugLines;
  std::vector<renderer::Light> m_lights;
  renderer::SceneView m_view;
  // The Game view's: the scene's camera over the same draws, a distinct object (ADR-0021).
  renderer::SceneView m_gameView;
  bool m_warnedAboutCamera{false};
  std::vector<std::uint32_t> m_outlineIds;
  std::optional<std::pair<std::filesystem::path, std::filesystem::path>> m_screenshotRequest; // viewport, window
  runtime::Screenshots m_screenshots;
  std::optional<core::Result<void>> m_screenshotResult;

  Selection m_selection;
  CommandStack m_commands;
  CommandStack m_historyBeforePlay; // the edit history play set aside, which stop puts back
  Gizmo m_gizmo;
  bool m_snapEnabled{false}; // Ctrl held during a drag inverts it
  std::filesystem::path m_preferencesFile;
  Preferences m_preferences;
  std::optional<assets::Project> m_project;
  std::filesystem::path m_scenePath;
  std::filesystem::path m_recoveryRoot;
  std::unique_ptr<Recovery> m_recovery;
  std::vector<SceneTab> m_tabs;
  std::size_t m_activeTab{0};
  std::size_t m_closingTab{0};
  std::uint64_t m_nextTabId{1};
  bool m_selectActiveTab{false}; // the tab bar follows a change the editor made, not the user's click
  nlohmann::json m_snapshot;
  std::function<void()> m_afterStop; // what the stop dialog was asked on behalf of
  std::size_t m_skippedChanges{0};
  bool m_stopAnswered{false};    // the dialog stays to report skipped edits after the answer
  bool m_dirtyBeforePlay{false}; // stop puts the snapshot back, so the scene is as saved as it was
  std::unique_ptr<ShaderCompiler> m_shaderCompiler;
  std::filesystem::path m_shaderSources;
  std::unordered_map<std::string, std::filesystem::file_time_type> m_shaderTimes;
  std::chrono::steady_clock::time_point m_lastShaderPoll{};
  bool m_shaderPollDisabled{false};

  LogPanel m_logPanel;
  StatisticsPanel m_statisticsPanel;
  ViewportPanel m_viewportPanel;
  GamePanel m_gamePanel;
  HierarchyPanel m_hierarchyPanel;
  InspectorPanel m_inspectorPanel;
  AssetBrowserPanel m_assetBrowserPanel;

  glm::vec2 m_lookDelta{0.0f, 0.0f};
  glm::vec2 m_mouseBeforeLook{0.0f, 0.0f};
  std::optional<Selection::Mode> m_pendingPickMode;
  Modal m_modal{Modal::None};
  FileDialog m_fileDialog{makeNoFileDialogBackend()};
  // What the chooser in flight was opened for, and the modal or tab it was opened in, which the
  // result is only applied to while they still stand.
  DialogTarget m_dialogTarget{};
  Modal m_dialogModal{Modal::None};
  std::size_t m_dialogTab{0};
  std::string m_modalPath;
  std::string m_modalName;
  std::string m_modalError;
  std::string m_modalMessage; // what the last export did, shown in its dialog
  assets::CookPlatform m_exportPlatform{assets::hostPlatform()};
  bool m_exportCurrentScene{false};
  std::string m_title;
  bool m_relativeMouseRequested{false};
  bool m_layoutBuilt{false};
  bool m_layoutRequested{false}; // a built-in layout, even over a restored one
  BuiltinLayout m_builtin{BuiltinLayout::Default};
  std::optional<std::string> m_pendingLayout; // a layout to apply before the next frame
  std::string m_layoutFile;                   // io.IniFilename points at this; empty keeps no layout on disk
  std::filesystem::path m_layoutsDirectory;   // the saved presets; empty keeps none
  std::string m_layoutName;                   // the name typed in the Save layout dialog
  bool m_layoutOverwrite{false};              // the dialog is asking before it replaces a preset
  // Closed at start: the second view costs a full view of rendering while it is on screen (ADR-0021).
  ViewState m_viewState;
  ViewState m_savedView; // what the ini was last told, to notice a change
  LayoutSettings m_layoutSettings{m_viewState};
  bool m_gameInputWasActive{false};
  WindowOrigins m_windowOrigins;
  bool m_quitRequested{false};
  float m_frameMilliseconds{0.0f};
};

} // namespace sonnet::editor
