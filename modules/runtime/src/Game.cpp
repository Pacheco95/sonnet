#include <sonnet/runtime/Game.h>

#include <sonnet/assets/Bundle.h>
#include <sonnet/core/File.h>
#include <sonnet/core/Log.h>
#include <sonnet/core/Profile.h>
#include <sonnet/world/Scene.h>

#include <format>
#include <utility>

namespace sonnet::runtime {

namespace {

using nlohmann::json;

// Where a scene with no Camera is watched from: back from the origin and looking at it, so a
// scene that forgot one still shows something rather than nothing.
[[nodiscard]] renderer::Camera fallbackCamera() {
  return {.position = {6.0f, 4.0f, 8.0f},
          .rotation = glm::quatLookAt(glm::normalize(glm::vec3{0.0f, 0.5f, 0.0f} - glm::vec3{6.0f, 4.0f, 8.0f}),
                                      glm::vec3{0.0f, 1.0f, 0.0f})};
}

[[nodiscard]] core::Result<json> parseJsonFile(const std::filesystem::path &file) {
  const auto bytes = core::readFile(file);
  if (!bytes) {
    return std::unexpected(bytes.error());
  }
  json document = json::parse(bytes->begin(), bytes->end(), nullptr, false);
  if (document.is_discarded()) {
    return std::unexpected(core::Error{std::format("{}: not valid JSON", file.string()), core::ErrorCategory::Io});
  }
  return document;
}

} // namespace

Game::Game(platform::Platform &platform, platform::IWindow &window, rhi::IDevice &device,
           const rhi::ISwapchain &swapchain, const GameDesc &desc)
    : m_window(window), m_device(device), m_renderer(device, platform.basePath() / "shaders",
                                                     [&] {
                                                       // The present pass writes the acquired image, so its pipeline is
                                                       // built for the swapchain's format (docs/rendering.md, "Frame
                                                       // structure").
                                                       renderer::RendererSettings settings = desc.renderer;
                                                       settings.presentFormat = swapchain.format();
                                                       return settings;
                                                     }()),
      m_graph(device), m_target(device, "game"), m_assets(m_renderer), m_world({}),
      m_physics(physics::createPhysicsWorld(m_world, m_assets)),
      m_scripts(scripting::createScriptRuntime(
          {.world = &m_world, .assets = &m_assets, .physics = m_physics.get(), .input = &m_input})),
      m_animation(m_world, m_assets),
      m_audio(audio::createAudioDevice(m_world, m_assets, {.output = desc.audioOutput})) {
  // A player is always playing: there is no edit mode to switch out of.
  m_world.setPlaying(true);
  m_view.camera = fallbackCamera();
}

Game::~Game() {
  m_device.waitIdle();
}

void Game::reset() {
  m_world.setPlaying(false);
  m_world.clearScene();
  m_world.clearPrefabs();
  m_scripts->reset();
  m_audio->stopAll();
  m_name.clear();
  m_warnedAboutCamera = false;
}

core::Result<void> Game::open(const std::filesystem::path &path) {
  std::error_code error;
  if (std::filesystem::is_directory(path, error)) {
    return openProject(path);
  }
  return openBundle(path);
}

core::Result<void> Game::openProject(const std::filesystem::path &directory) {
  SONNET_ZONE();
  auto project = assets::Project::open(directory);
  if (!project) {
    return std::unexpected(project.error());
  }
  reset();
  m_assets.open(project->root, project->assetRoots);
  m_name = project->name;
  loadPrefabs();
  const auto scene = parseJsonFile(project->resolve(project->startScene));
  if (!scene) {
    return std::unexpected(scene.error());
  }
  return startScene(*scene);
}

core::Result<void> Game::openBundle(const std::filesystem::path &file) {
  SONNET_ZONE();
  reset();
  if (const auto opened = m_assets.openBundle(file); !opened) {
    return std::unexpected(opened.error());
  }
  const assets::Bundle &bundle = *m_assets.bundle();
  m_name = bundle.manifest().name;
  loadPrefabs();
  const auto payload = bundle.read(bundle.manifest().startScene);
  if (!payload) {
    return std::unexpected(payload.error());
  }
  const auto scene = assets::decodeJson(*payload);
  if (!scene) {
    return std::unexpected(scene.error());
  }
  return startScene(*scene);
}

void Game::loadPrefabs() {
  // Authored prefabs first: a scene's instances need their base loaded.
  if (const assets::Bundle *bundle = m_assets.bundle()) {
    for (const std::string &path : bundle->files()) {
      if (!path.ends_with(".prefab.json")) {
        continue;
      }
      const auto payload = bundle->read(path);
      const auto document = payload ? assets::decodeJson(*payload) : std::unexpected(payload.error());
      const auto loaded = document ? world::loadPrefab(m_world, *document) : std::unexpected(document.error());
      if (!loaded) {
        SONNET_LOG_ERROR("{}: {}", path, loaded.error().toString());
      }
    }
  } else {
    // The project's prefab files live anywhere under it, as they do for the editor.
    assets::Project project;
    project.root = m_assets.projectRoot();
    for (const std::filesystem::path &file : project.files(".prefab.json")) {
      if (const auto loaded = world::loadPrefabFile(m_world, file); !loaded) {
        SONNET_LOG_ERROR("{}", loaded.error().toString());
      }
    }
  }
  loadModelPrefabs();
}

void Game::loadModelPrefabs() {
  // Every model is a prefab too, under its own identity, so scenes can place one
  // (docs/world.md, "Prefabs").
  for (const assets::AssetInfo *info : m_assets.assets(assets::AssetType::Model)) {
    if (const assets::Model *model = m_assets.model(info->uuid)) {
      static_cast<void>(world::loadModelPrefab(m_world, *model, info->uuid, info->name));
    }
  }
}

core::Result<void> Game::startScene(const json &scene) {
  const auto loaded = world::loadScene(m_world, scene);
  if (!loaded) {
    return std::unexpected(loaded.error());
  }
  m_world.setPlaying(true);
  m_window.setTitle(m_name.empty() ? std::string_view{"Sonnet"} : std::string_view{m_name});
  SONNET_LOG_INFO("playing \"{}\": {} entities", m_name, loaded->size());
  return {};
}

void Game::event(const platform::Event &event) {
  // Everything the window gets is the game's; there is no editor panel to compete for it.
  m_input.handle(event);
}

void Game::update(float dt) {
  SONNET_ZONE();
  // Hot reload belongs to the editor, and a bundle has nothing to watch; in project mode a
  // changed source still reaches the next frame, which makes running a project folder a usable
  // way to try a change without the editor.
  static_cast<void>(m_assets.pollChanges());

  const std::optional<renderer::Camera> scene = world::sceneCamera(m_world);
  if (!scene && !m_warnedAboutCamera) {
    SONNET_LOG_WARN("the scene has no Camera; drawing from the fallback view");
    m_warnedAboutCamera = true;
  }
  const renderer::Camera camera = scene.value_or(fallbackCamera());
  // A scene without an AudioListener is heard from the camera, as it is in the editor.
  m_audio->setFallbackListener(camera.position, camera.rotation);

  m_world.progress(dt);
  m_input.beginFrame();

  world::buildDrawList(m_world, m_assets, m_draws, m_joints);
  world::buildLightList(m_world, m_lights);
  m_view.camera = camera;
  m_view.draws = m_draws;
  m_view.joints = m_joints;
  m_view.lights = m_lights;
  const std::optional<renderer::DirectionalLight> sun = world::sceneLight(m_world);
  m_view.hasSun = sun.has_value();
  m_view.sun = sun.value_or(renderer::DirectionalLight{});
  const std::optional<world::SceneEnvironment> environment = world::sceneEnvironment(m_world, m_assets);
  m_view.environment = environment ? environment->environment : renderer::EnvironmentHandle{};
  m_view.environmentIntensity = environment ? environment->intensity : 1.0f;
  m_view.exposure = environment ? environment->exposure : 1.0f;
  m_view.debugLines = {};
}

void Game::render(rhi::ICommandList &commands, const std::optional<rhi::SwapchainImage> &swapchainImage) {
  SONNET_ZONE();
  m_graph.reset();
  if (!swapchainImage) {
    // Minimised, or the swapchain is being recreated: the simulation ran, nothing is drawn.
    m_graph.execute(commands);
    return;
  }
  // The scene is drawn at the window's size in the renderer's own formats, then copied into the
  // acquired image, whose format the surface chose.
  m_target.resize(swapchainImage->extent);
  const renderer::GraphImage backbuffer = m_graph.importImage(swapchainImage->image, rhi::ImageLayout::Present);
  if (m_target.isValid()) {
    const renderer::GraphImage color = m_graph.importImage(m_target.color());
    const renderer::GraphImage depth = m_graph.importImage(m_target.depth());
    m_renderer.addScenePasses(m_graph, m_view, color, depth);
    m_renderer.addPresentPass(m_graph, color, backbuffer);
  }
  m_graph.execute(commands);
}

} // namespace sonnet::runtime
