#include <sonnet/editor/Editor.h>

#include <sonnet/editor/EntityCommands.h>

#include <sonnet/core/Log.h>
#include <sonnet/core/Profile.h>
#include <sonnet/core/Version.h>
#include <sonnet/world/Scene.h>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cstdlib>
#include <format>
#include <fstream>
#include <thread>
#include <variant>

namespace sonnet::editor {

namespace {

constexpr const char *ModalId = "Project";

std::string nameOf(flecs::entity entity) {
  const world::Name *name = entity ? entity.try_get<world::Name>() : nullptr;
  return name != nullptr ? name->value : std::string{};
}

} // namespace

Editor::Editor(platform::Platform &platform, platform::IWindow &window, rhi::IDevice &device,
               const rhi::ISwapchain &swapchain, bool explorer)
    : m_window(window), m_device(device), m_basePath(platform.basePath()),
      m_imgui({.window = &window,
               .device = &device,
               .swapchainFormat = swapchain.format(),
               .swapchainImageCount = swapchain.imageCount(),
               .docking = true,
               // Platform windows need a display; the headless driver has none.
               .viewports = !platform.isHeadless()}),
      m_renderer(device, platform.basePath() / "shaders"), m_graph(device), m_picker(device), m_assets(m_renderer),
      m_world({.explorer = explorer}), m_preferencesFile(platform.prefPath("sonnet", "editor") / "preferences.json"),
      m_preferences(Preferences::load(m_preferencesFile)), m_viewportPanel(device, m_imgui),
      m_hierarchyPanel(m_world, m_selection, m_commands), m_inspectorPanel(m_world, m_assets, m_selection, m_commands),
      m_assetBrowserPanel(m_assets, m_selection) {
  m_logPanel.setLocationHandler([this](const std::string &path, int line) { openLocation(path, line); });
  newScene();
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
  m_selection.prune(m_world);

  m_imgui.beginFrame();
  const ImGuiID dockspace = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
  if (!m_layoutBuilt) {
    buildDefaultLayout(dockspace);
    m_layoutBuilt = true;
  }
  drawMenuBar();
  handleShortcuts();
  drawModal();

  if (m_showHierarchy) {
    m_hierarchyPanel.draw(m_showHierarchy);
  }
  if (m_showInspector) {
    m_inspectorPanel.draw(m_showInspector, m_assetBrowserPanel.inspected());
  }
  if (m_showAssets) {
    m_assetBrowserPanel.draw(m_showAssets);
  }
  if (m_showViewport) {
    const bool wantsRelativeMouse =
        m_viewportPanel.draw(m_showViewport, dt, m_lookDelta, m_showOverlay ? &m_statisticsPanel : nullptr,
                             [this](const ViewportInput &input) { drawViewportOverlay(input); });
    // Requested on change, not by comparing with the window's state: a platform that refuses the
    // mode would otherwise be asked, and would warn, every frame.
    if (wantsRelativeMouse != m_relativeMouseRequested) {
      m_relativeMouseRequested = wantsRelativeMouse;
      static_cast<void>(m_window.setRelativeMouseMode(wantsRelativeMouse));
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

  // Changed source files are re-imported before the draw list resolves them.
  static_cast<void>(m_assets.pollChanges());
  // The world's frame after the UI edited it: systems, then what the renderer draws.
  m_world.progress(dt);
  world::buildDrawList(m_world, m_assets, m_draws);
  world::buildLightList(m_world, m_lights);
  m_view.camera = m_viewportPanel.camera().camera();
  m_view.draws = m_draws;
  m_view.lights = m_lights;
  const std::optional<renderer::DirectionalLight> sun = world::sceneLight(m_world);
  m_view.hasSun = sun.has_value();
  m_view.sun = sun.value_or(renderer::DirectionalLight{});
  const std::optional<world::SceneEnvironment> environment = world::sceneEnvironment(m_world, m_assets);
  m_view.environment = environment ? environment->environment : renderer::EnvironmentHandle{};
  m_view.environmentIntensity = environment ? environment->intensity : 1.0f;
  m_view.exposure = environment ? environment->exposure : 1.0f;
  // The selection and everything under it: selecting a parent outlines its whole subtree.
  m_outlineIds.clear();
  std::vector<flecs::entity> pending;
  for (const core::Uuid uuid : m_selection.items()) {
    if (const flecs::entity entity = m_world.find(uuid)) {
      pending.push_back(entity);
    }
  }
  while (!pending.empty()) {
    const flecs::entity entity = pending.back();
    pending.pop_back();
    m_outlineIds.push_back(world::World::pickId(entity));
    std::ranges::copy(m_world.children(entity), std::back_inserter(pending));
  }
  updateTitle();
}

void Editor::drawViewportOverlay(const ViewportInput &input) {
  const bool cameraActive = m_viewportPanel.cameraActive();
  const glm::uvec2 targetSize = m_viewportPanel.target().size();
  const float aspect = targetSize.y > 0 ? static_cast<float>(targetSize.x) / static_cast<float>(targetSize.y) : 1.0f;
  const renderer::Camera &camera = m_viewportPanel.camera().camera();
  const GizmoView view{.view = camera.view(),
                       .projection = camera.projection(aspect),
                       .cameraPosition = camera.position,
                       .origin = input.origin,
                       .size = input.size,
                       .mouse = input.mouse,
                       .mouseDown = input.leftDown && !cameraActive,
                       .mouseClicked = input.leftClicked && !cameraActive};
  const flecs::entity primary = m_world.find(m_selection.primary());
  const GizmoResult gizmo = m_gizmo.update(view, m_world, primary, input.drawList);
  if (gizmo.finished && primary) {
    const world::ComponentInfo *transform = m_world.findComponent("Transform");
    const char *verb = m_gizmo.mode() == GizmoMode::Translate ? "move"
                       : m_gizmo.mode() == GizmoMode::Rotate  ? "rotate"
                                                              : "scale";
    m_commands.push(componentCommand(m_selection.primary(), transform->name,
                                     std::make_optional(m_world.valueToJson(transform->id, &gizmo.before)),
                                     std::make_optional(m_world.componentToJson(primary, transform->id)),
                                     std::format("{} {}", verb, nameOf(primary))),
                    m_world);
  }
  if (input.leftClicked && !gizmo.hovered && !gizmo.active && !cameraActive) {
    const ImGuiIO &io = ImGui::GetIO();
    m_picker.request(m_viewportPanel.targetPixel(input.mouse));
    m_pendingPickMode = io.KeyCtrl    ? Selection::Mode::Toggle
                        : io.KeyShift ? Selection::Mode::Add
                                      : Selection::Mode::Replace;
  }
}

void Editor::applyPick(std::uint32_t id) {
  const Selection::Mode mode = m_pendingPickMode.value_or(Selection::Mode::Replace);
  m_pendingPickMode.reset();
  const flecs::entity entity = m_world.fromPickId(id);
  if (entity && entity.has<world::Identity>()) {
    m_selection.select(m_world.uuidOf(entity), mode);
  } else if (mode == Selection::Mode::Replace) {
    m_selection.clear();
  }
}

void Editor::handleShortcuts() {
  const ImGuiIO &io = ImGui::GetIO();
  if (io.WantTextInput || m_modal != Modal::None) {
    return;
  }
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z)) {
    m_commands.undo(m_world);
  }
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y) ||
      ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z)) {
    m_commands.redo(m_world);
  }
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) {
    if (m_scenePath.empty()) {
      m_modal = Modal::SaveSceneAs;
    } else if (const auto saved = saveScene(); !saved) {
      SONNET_LOG_ERROR("{}", saved.error().toString());
    }
  }
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_P)) {
    isPlaying() ? stop() : play();
  }
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_D)) {
    m_hierarchyPanel.duplicateSelection();
  }
  if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
    m_hierarchyPanel.deleteSelection();
  }
  if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
    m_selection.clear();
  }
  if (m_viewportPanel.input().hovered && !m_viewportPanel.cameraActive()) {
    if (ImGui::IsKeyPressed(ImGuiKey_W, false)) {
      m_gizmo.setMode(GizmoMode::Translate);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_E, false)) {
      m_gizmo.setMode(GizmoMode::Rotate);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_R, false)) {
      m_gizmo.setMode(GizmoMode::Scale);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F, false)) {
      focusSelection();
    }
  }
}

void Editor::drawMenuBar() {
  if (!ImGui::BeginMainMenuBar()) {
    return;
  }
  if (ImGui::BeginMenu("File")) {
    if (ImGui::MenuItem("New project...")) {
      m_modal = Modal::NewProject;
    }
    if (ImGui::MenuItem("Open project...")) {
      m_modal = Modal::OpenProject;
    }
    if (ImGui::BeginMenu("Recent projects", !m_preferences.recentProjects.empty())) {
      // A copy: opening a project reorders the list being shown.
      const std::vector<std::filesystem::path> recents = m_preferences.recentProjects;
      for (const std::filesystem::path &recent : recents) {
        if (ImGui::MenuItem(recent.generic_string().c_str())) {
          if (const auto opened = openProject(recent); !opened) {
            SONNET_LOG_ERROR("{}", opened.error().toString());
          }
        }
      }
      ImGui::EndMenu();
    }
    ImGui::Separator();
    if (ImGui::MenuItem("New scene")) {
      newScene();
    }
    if (ImGui::BeginMenu("Open scene", m_project.has_value())) {
      for (const std::filesystem::path &scene : m_project->files(".scene.json")) {
        if (ImGui::MenuItem(m_project->relative(scene).c_str())) {
          if (const auto opened = openScene(scene); !opened) {
            SONNET_LOG_ERROR("{}", opened.error().toString());
          }
        }
      }
      ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Save scene", "Ctrl+S")) {
      if (m_scenePath.empty()) {
        m_modal = Modal::SaveSceneAs;
      } else if (const auto saved = saveScene(); !saved) {
        SONNET_LOG_ERROR("{}", saved.error().toString());
      }
    }
    if (ImGui::MenuItem("Save scene as...")) {
      m_modal = Modal::SaveSceneAs;
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Quit", "Ctrl+Q")) {
      m_quitRequested = true;
    }
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Edit")) {
    const std::string undo = std::format("Undo {}", m_commands.undoDescription());
    const std::string redo = std::format("Redo {}", m_commands.redoDescription());
    if (ImGui::MenuItem(undo.c_str(), "Ctrl+Z", false, m_commands.canUndo())) {
      m_commands.undo(m_world);
    }
    if (ImGui::MenuItem(redo.c_str(), "Ctrl+Y", false, m_commands.canRedo())) {
      m_commands.redo(m_world);
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, !m_selection.empty())) {
      m_hierarchyPanel.duplicateSelection();
    }
    if (ImGui::MenuItem("Delete", "Del", false, !m_selection.empty())) {
      m_hierarchyPanel.deleteSelection();
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Translate", "W", m_gizmo.mode() == GizmoMode::Translate)) {
      m_gizmo.setMode(GizmoMode::Translate);
    }
    if (ImGui::MenuItem("Rotate", "E", m_gizmo.mode() == GizmoMode::Rotate)) {
      m_gizmo.setMode(GizmoMode::Rotate);
    }
    if (ImGui::MenuItem("Scale", "R", m_gizmo.mode() == GizmoMode::Scale)) {
      m_gizmo.setMode(GizmoMode::Scale);
    }
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Play")) {
    if (ImGui::MenuItem(isPlaying() ? "Stop" : "Play", "Ctrl+P")) {
      isPlaying() ? stop() : play();
    }
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("View")) {
    ImGui::MenuItem("Viewport", nullptr, &m_showViewport);
    ImGui::MenuItem("Hierarchy", nullptr, &m_showHierarchy);
    ImGui::MenuItem("Inspector", nullptr, &m_showInspector);
    ImGui::MenuItem("Log", nullptr, &m_showLog);
    ImGui::MenuItem("Assets", nullptr, &m_showAssets);
    ImGui::MenuItem("Statistics", nullptr, &m_showStatistics);
    ImGui::MenuItem("Statistics overlay", nullptr, &m_showOverlay);
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Help")) {
    ImGui::MenuItem(("Sonnet " + core::engineVersion().toString()).c_str(), nullptr, false, false);
    ImGui::EndMenu();
  }
  // The play control sits on the bar itself, where it is always in view.
  // Sampled once: the click below toggles the state, and the pop has to match the push.
  const bool playing = isPlaying();
  const char *label = playing ? "  Stop  " : "  Play  ";
  ImGui::SetCursorPosX(ImGui::GetWindowWidth() * 0.5f - ImGui::CalcTextSize(label).x * 0.5f);
  if (playing) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.7f, 0.25f, 0.2f, 1.0f});
  }
  if (ImGui::SmallButton(label)) {
    playing ? stop() : play();
  }
  if (playing) {
    ImGui::PopStyleColor();
  }
  ImGui::EndMainMenuBar();
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Q)) {
    m_quitRequested = true;
  }
}

void Editor::drawModal() {
  if (m_modal == Modal::None) {
    return;
  }
  if (!ImGui::IsPopupOpen(ModalId)) {
    ImGui::OpenPopup(ModalId);
    m_modalError.clear();
    if (m_modal == Modal::SaveSceneAs) {
      m_modalPath = m_scenePath.empty() && m_project ? m_project->resolve("scenes/untitled.scene.json").string()
                                                     : m_scenePath.string();
    } else if (m_project) {
      m_modalPath = m_project->root.parent_path().string();
    }
  }
  ImGui::SetNextWindowSize(ImVec2{520.0f, 0.0f});
  if (!ImGui::BeginPopupModal(ModalId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    return;
  }
  const char *title = m_modal == Modal::NewProject    ? "Create a project folder"
                      : m_modal == Modal::OpenProject ? "Open a project folder"
                                                      : "Save the scene as";
  ImGui::TextUnformatted(title);
  if (m_modal == Modal::NewProject) {
    ImGui::InputText("Name", &m_modalName);
  }
  ImGui::InputText("Path", &m_modalPath);
  if (!m_modalError.empty()) {
    ImGui::TextColored(ImVec4{0.95f, 0.4f, 0.4f, 1.0f}, "%s", m_modalError.c_str());
  }
  const bool confirmed = ImGui::Button("OK", ImVec2{120.0f, 0.0f}) || ImGui::IsKeyPressed(ImGuiKey_Enter, false);
  ImGui::SameLine();
  const bool cancelled = ImGui::Button("Cancel", ImVec2{120.0f, 0.0f}) || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
  if (confirmed && !m_modalPath.empty()) {
    core::Result<void> outcome;
    switch (m_modal) {
    case Modal::NewProject:
      outcome = createProject(m_modalPath, m_modalName);
      break;
    case Modal::OpenProject:
      outcome = openProject(m_modalPath);
      break;
    case Modal::SaveSceneAs:
      outcome = saveSceneAs(m_modalPath);
      break;
    case Modal::None:
      break;
    }
    if (outcome) {
      m_modal = Modal::None;
      ImGui::CloseCurrentPopup();
    } else {
      m_modalError = outcome.error().message;
    }
  } else if (cancelled) {
    m_modal = Modal::None;
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

void Editor::buildDefaultLayout(unsigned dockspace) {
  // Hierarchy on the left, viewport in the centre, inspector over statistics on the right, log
  // along the bottom.
  ImGui::DockBuilderRemoveNode(dockspace);
  ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dockspace, ImGui::GetMainViewport()->WorkSize);
  ImGuiID centre = dockspace;
  const ImGuiID bottom = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down, 0.22f, nullptr, &centre);
  const ImGuiID left = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Left, 0.18f, nullptr, &centre);
  ImGuiID right = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, 0.26f, nullptr, &centre);
  const ImGuiID rightBottom = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.35f, nullptr, &right);
  ImGui::DockBuilderDockWindow("Viewport", centre);
  ImGui::DockBuilderDockWindow("Hierarchy", left);
  ImGui::DockBuilderDockWindow("Inspector", right);
  ImGui::DockBuilderDockWindow("Statistics", rightBottom);
  ImGui::DockBuilderDockWindow("Log", bottom);
  ImGui::DockBuilderDockWindow("Assets", bottom);
  ImGui::DockBuilderFinish(dockspace);
}

void Editor::render(rhi::ICommandList &commands, const std::optional<rhi::SwapchainImage> &swapchainImage) {
  SONNET_ZONE();
  if (const std::optional<std::uint32_t> picked = m_picker.poll()) {
    applyPick(*picked);
  }
  m_graph.reset();
  renderer::RenderTarget &target = m_viewportPanel.target();
  renderer::GraphImage sceneColor;
  if (target.isValid()) {
    sceneColor = m_graph.importImage(target.color());
    const renderer::GraphImage depth = m_graph.importImage(target.depth());
    const rhi::ImageDesc idDesc{.size = target.size(),
                                .format = renderer::Renderer::IdFormat,
                                .usage = rhi::ImageUsage::None,
                                .debugName = "ids"};
    const renderer::GraphImage ids = m_graph.createImage(idDesc);
    rhi::ImageDesc maskDesc = idDesc;
    maskDesc.debugName = "selection mask";
    const renderer::GraphImage mask = m_graph.createImage(maskDesc);
    m_renderer.addScenePasses(m_graph, m_view, sceneColor, depth);
    m_renderer.addIdPass(m_graph, m_view, ids, depth);
    m_renderer.addSelectionMaskPass(m_graph, m_view, mask, m_outlineIds);
    m_renderer.addOutlinePass(m_graph, sceneColor, mask);
    m_picker.addPass(m_graph, ids, target.size());
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

core::Result<void> Editor::openProject(const std::filesystem::path &directory) {
  auto project = Project::open(directory);
  if (!project) {
    return std::unexpected(project.error());
  }
  if (isPlaying()) {
    stop();
  }
  m_project = std::move(*project);
  m_assetBrowserPanel.inspect({});
  m_preferences.addRecentProject(m_project->root);
  if (const auto saved = m_preferences.save(m_preferencesFile); !saved) {
    SONNET_LOG_WARN("{}", saved.error().toString());
  }
  m_world.clearScene();
  m_world.clearPrefabs();
  m_assets.open(m_project->root, m_project->assetRoots);
  loadPrefabs();
  const std::filesystem::path scene = m_project->resolve(m_project->startScene);
  if (const auto opened = openScene(scene); !opened) {
    newScene();
    return opened;
  }
  return {};
}

core::Result<void> Editor::createProject(const std::filesystem::path &directory, std::string name) {
  const auto project = Project::create(directory, std::move(name));
  if (!project) {
    return std::unexpected(project.error());
  }
  return openProject(project->root);
}

void Editor::loadPrefabs() {
  if (!m_project) {
    return;
  }
  for (const std::filesystem::path &file : m_project->files(".prefab.json")) {
    if (const auto loaded = world::loadPrefabFile(m_world, file); !loaded) {
      SONNET_LOG_ERROR("{}", loaded.error().toString());
    }
  }
  // Every glTF file is a prefab too, under the model's identity, so scenes can place it.
  for (const assets::AssetInfo *info : m_assets.assets(assets::AssetType::Model)) {
    if (const assets::Model *model = m_assets.model(info->uuid)) {
      static_cast<void>(world::loadModelPrefab(m_world, *model, info->uuid, info->name));
    }
  }
}

core::Result<void> Editor::loadSceneFile(const std::filesystem::path &file) {
  if (isPlaying()) {
    stop();
  }
  m_world.clearScene();
  m_selection.clear();
  m_commands.clear();
  const auto loaded = world::loadSceneFile(m_world, file);
  if (!loaded) {
    return std::unexpected(loaded.error());
  }
  return {};
}

core::Result<void> Editor::openScene(const std::filesystem::path &file) {
  if (const auto loaded = loadSceneFile(file); !loaded) {
    newScene();
    return loaded;
  }
  m_scenePath = std::filesystem::absolute(file).lexically_normal();
  markSaved();
  return {};
}

core::Result<void> Editor::saveScene() {
  if (m_scenePath.empty()) {
    return std::unexpected(core::Error{"the scene has no file yet: use Save scene as", core::ErrorCategory::Io});
  }
  // In play mode the edited scene is the snapshot, not the running one.
  const nlohmann::json scene = isPlaying() ? m_snapshot : world::saveScene(m_world);
  std::error_code error;
  std::filesystem::create_directories(m_scenePath.parent_path(), error);
  std::ofstream stream{m_scenePath};
  if (!stream) {
    return std::unexpected(
        core::Error{std::format("{}: cannot open for writing", m_scenePath.string()), core::ErrorCategory::Io});
  }
  stream << scene.dump(2) << '\n';
  if (!stream) {
    return std::unexpected(core::Error{std::format("{}: write failed", m_scenePath.string()), core::ErrorCategory::Io});
  }
  markSaved();
  SONNET_LOG_INFO("saved {}", m_scenePath.string());
  return {};
}

core::Result<void> Editor::saveSceneAs(const std::filesystem::path &file) {
  const std::filesystem::path previous = m_scenePath;
  m_scenePath = std::filesystem::absolute(file).lexically_normal();
  if (const auto saved = saveScene(); !saved) {
    m_scenePath = previous;
    return saved;
  }
  return {};
}

void Editor::newScene() {
  if (isPlaying()) {
    stop();
  }
  m_world.clearScene();
  m_selection.clear();
  m_commands.clear();
  if (const auto loaded = world::loadScene(m_world, starterScene()); !loaded) {
    SONNET_LOG_ERROR("starter scene: {}", loaded.error().toString());
  }
  m_scenePath.clear();
  markSaved();
}

void Editor::play() {
  if (isPlaying()) {
    return;
  }
  m_snapshot = world::saveScene(m_world);
  m_world.setPlaying(true);
  SONNET_LOG_INFO("play");
}

void Editor::stop() {
  if (!isPlaying()) {
    return;
  }
  const bool wasDirty = isDirty();
  m_world.setPlaying(false);
  m_world.clearScene();
  if (const auto restored = world::loadScene(m_world, m_snapshot); !restored) {
    SONNET_LOG_ERROR("restoring the scene after play: {}", restored.error().toString());
  }
  // Edits made while playing are gone with the snapshot, and so is their history.
  m_commands.clear();
  if (!wasDirty) {
    markSaved();
  }
  m_selection.prune(m_world);
  SONNET_LOG_INFO("stop");
}

void Editor::markSaved() {
  m_savedRevision = m_commands.revision();
}

void Editor::updateTitle() {
  std::string title = "Sonnet Editor";
  if (m_project) {
    title += " - " + m_project->name;
  }
  title += " - " + (m_scenePath.empty() ? std::string{"untitled"} : m_scenePath.filename().string());
  if (isDirty()) {
    title += "*";
  }
  if (isPlaying()) {
    title += " [playing]";
  }
  if (title != m_title) {
    m_title = title;
    m_window.setTitle(title);
  }
}

void Editor::focusSelection() {
  const flecs::entity primary = m_world.find(m_selection.primary());
  if (!primary) {
    return;
  }
  const world::WorldTransform *transform = primary.try_get<world::WorldTransform>();
  const glm::mat4 matrix = transform != nullptr ? transform->matrix : glm::mat4{1.0f};
  const world::Transform placed = world::Transform::fromMatrix(matrix);
  const float radius = std::max({std::abs(placed.scale.x), std::abs(placed.scale.y), std::abs(placed.scale.z)}) * 0.7f;
  m_viewportPanel.focus(placed.position, radius);
}

void Editor::openLocation(const std::string &path, int line) {
  const std::optional<std::filesystem::path> file = locateSource(path, m_preferences.sourceRoot, m_basePath);
  if (!file) {
    SONNET_LOG_WARN("{} was not found from {}; set sourceRoot in {}", path, m_basePath.string(),
                    m_preferencesFile.string());
    return;
  }
  const std::string command = m_preferences.editorCommand(*file, line);
  SONNET_LOG_INFO("opening {}:{} with: {}", file->string(), line, command);
  // Detached so a slow editor start never blocks the frame; the exit code is not interesting.
  std::thread{[command] { static_cast<void>(std::system(command.c_str())); }}.detach();
}

} // namespace sonnet::editor
