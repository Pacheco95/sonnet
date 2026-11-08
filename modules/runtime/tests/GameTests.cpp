#include <sonnet/runtime/Capture.h>
#include <sonnet/runtime/Game.h>

#include <sonnet/assets/Bundle.h>
#include <sonnet/assets/Cook.h>
#include <sonnet/assets/Importers.h>
#include <sonnet/assets/Json.h>
#include <sonnet/core/Error.h>
#include <sonnet/core/File.h>
#include <sonnet/core/Log.h>
#include <sonnet/gui/Components.h>
#include <sonnet/physics/Components.h>
#include <sonnet/platform/Platform.h>
#include <sonnet/rhi/Device.h>
#include <sonnet/rhi/NullDevice.h>
#include <sonnet/rhi/Swapchain.h>
#include <sonnet/world/Components.h>
#include <sonnet/world/DrawList.h>
#include <sonnet/world/Scene.h>

#include <RelativeModeWindow.h>
#include <TestSupport.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <spdlog/sinks/base_sink.h>

#include <stb_image.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <memory>
#include <mutex>
#include <numeric>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

using namespace sonnet;
using Catch::Approx;

namespace {

// The sample the editor's play-mode tests use too: a real project with models, a skin, a clip,
// sounds, scripts and physics. Found in the checkout a build directory lives in, the way the
// log panel's source links are (docs/editor.md, "Projects and scenes"), and copied once per run
// into the process's temporary directory, whose asset cache the suite's cases then share, so the
// checkout's sample is never written and another suite running at once never reads this one's
// cache (issue #468). Empty when no checkout holds the sample.
[[nodiscard]] std::filesystem::path sampleProject(platform::Platform &platform, std::string_view name = "basic") {
  std::error_code error;
  for (std::filesystem::path base = std::filesystem::absolute(platform.basePath(), error); !base.empty();
       base = base.parent_path()) {
    const std::filesystem::path candidate = base / "apps" / "samples" / name;
    if (std::filesystem::is_regular_file(candidate / "project.json", error)) {
      const std::filesystem::path copy = std::filesystem::temp_directory_path() / "samples" / name;
      if (!std::filesystem::exists(copy)) {
        sonnet::test::copySample(candidate, copy);
      }
      return copy;
    }
    if (base == base.root_path()) {
      break;
    }
  }
  return {};
}

// The character sample, with its humanoid's images and the course's textures already in the copy's
// cache as 1x1 placeholders. Cooking the humanoid's thirteen 2048-texel images to UASTC is most of
// what playing the character costs: about 15 s in a Debug build and three minutes under ASan on
// CI's runners (issue #496). The course's twelve textures cook on the workers while the game plays,
// about 6 s in Debug, and slow every frame stepped meanwhile (issue #530). A texture is cooked again
// only when its cache entry is older than the file, and nothing the tests check looks at the
// surfaces' colours; the image path itself is assets_tests' to cover.
[[nodiscard]] std::filesystem::path characterProject(platform::Platform &platform) {
  const std::filesystem::path project = sampleProject(platform, "character");
  if (project.empty()) {
    return project;
  }
  const std::filesystem::path cache = project / ".sonnet" / "cache";
  std::filesystem::create_directories(cache);
  const auto placeholder = [&](const core::Uuid &texture, bool srgb) {
    const renderer::TextureData texel{.size = {1, 1},
                                      .format = srgb ? rhi::Format::R8G8B8A8Srgb : rhi::Format::R8G8B8A8Unorm,
                                      .mipLevels = 1,
                                      .data = std::vector<std::byte>(4, std::byte{0x80})};
    const auto cooked = assets::cookKtx2(texel, false);
    REQUIRE(cooked.has_value());
    REQUIRE(core::writeFile(cache / (texture.toString() + ".ktx2"), *cooked).has_value());
  };
  const std::filesystem::path model = project / "assets" / "character" / "humanoid.glb";
  const auto sidecar = core::readFile(std::filesystem::path{model}.concat(".meta"));
  REQUIRE(sidecar.has_value());
  const std::optional<core::Uuid> identity =
      core::Uuid::parse(assets::parseJson(*sidecar).at("uuid").get<std::string>());
  REQUIRE(identity.has_value());
  const auto imported = assets::importGltf(model);
  REQUIRE(imported.has_value());
  for (std::size_t i = 0; i < imported->images.size(); ++i) {
    placeholder(core::Uuid::derive(*identity, std::format("image/{}", i)), imported->images[i].srgb);
  }
  int textures = 0;
  for (const auto &entry : std::filesystem::directory_iterator(project / "assets" / "level" / "textures")) {
    if (entry.path().extension() != ".meta") {
      continue;
    }
    const auto text = core::readFile(entry.path());
    REQUIRE(text.has_value());
    const auto meta = assets::parseJson(*text);
    REQUIRE(meta.at("type").get<std::string>() == "Texture");
    const std::optional<core::Uuid> texture = core::Uuid::parse(meta.at("uuid").get<std::string>());
    REQUIRE(texture.has_value());
    placeholder(*texture, meta.at("settings").at("srgb").get<bool>());
    ++textures;
  }
  REQUIRE(textures == 12);
  // The sky has no cache entry: a bundle's map is cooked from the .hdr each time, which for the 2k
  // sky is most of a cook's minute in Debug (issue #497). A 4x2 grey sky in the copy keeps it short.
  std::string sky = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 4\n";
  for (int texel = 0; texel < 8; ++texel) {
    sky += "\x80\x80\x80\x81"; // RGBE for 1.0 in each channel
  }
  REQUIRE(core::writeFile(project / "assets" / "level" / "kloofendal_48d_partly_cloudy_2k.hdr", std::string_view{sky})
              .has_value());
  return project;
}

// Collects warnings and errors, so a frame that logs one fails the test that asked for silence.
class ProblemSink final : public spdlog::sinks::base_sink<std::mutex> {
public:
  std::vector<std::string> problems;

protected:
  void sink_it_(const spdlog::details::log_msg &message) override {
    if (message.level >= spdlog::level::warn) {
      problems.emplace_back(message.payload.data(), message.payload.size());
    }
  }
  void flush_() override {
  }
};

struct Fixture {
  platform::Platform platform{{.headless = true}};
  std::unique_ptr<platform::IWindow> window;
  std::unique_ptr<rhi::IDevice> device;
  std::unique_ptr<rhi::ISwapchain> swapchain;

  explicit Fixture(glm::uvec2 windowSize = {640, 480}) {
    try {
      window = platform.createWindow({.title = "runtime_tests", .size = windowSize});
      device = rhi::createDevice({.platform = &platform, .applicationName = "runtime_tests"});
    } catch (const core::Exception &e) {
      SKIP("no usable Vulkan 1.4 device: " << e.what());
    }
    const rhi::DeviceInfo &info = device->info();
    if (info.loaderVersion < VK_API_VERSION_1_4 && info.driverName != "llvmpipe") {
      SKIP("headless surfaces are not trusted on this loader and driver");
    }
    try {
      swapchain = device->createSwapchain(*window);
    } catch (const core::Exception &e) {
      SKIP("headless surfaces are not supported here: " << e.what());
    }
  }

  // Small everything, so Lavapipe finishes a frame quickly.
  [[nodiscard]] runtime::GameDesc desc() const {
    return {.audioOutput = false,
            .renderer = {.shadowMapSize = 256,
                         .shadowDistance = 30.0f,
                         .bloom = false,
                         .antialiasing = renderer::AntiAliasing::None,
                         .environmentSize = 8,
                         .maxEnvironmentSize = 8,
                         .irradianceSize = 4,
                         .irradianceSamples = 8,
                         .prefilteredSize = 8,
                         .prefilteredLevels = 2,
                         .prefilterSamples = 8,
                         .brdfLutSize = 16,
                         .brdfLutSamples = 8}};
  }

  void frame(runtime::Game &game, float dt = 1.0f / 60.0f) {
    game.update(dt);
    rhi::ICommandList &commands = device->beginFrame();
    const auto image = swapchain->acquire();
    REQUIRE(image.has_value());
    game.render(commands, image);
    device->endFrame();
    game.afterPresent();
  }

  // Frames at the capture's fixed step, as the player runs them, until the run ends.
  runtime::CaptureRun::Status capture(runtime::Game &game, runtime::CaptureRun &run) {
    runtime::GameCaptureTarget target{game};
    runtime::CaptureRun::Status status = runtime::CaptureRun::Status::Running;
    for (int i = 0; i < 600 && status == runtime::CaptureRun::Status::Running; ++i) {
      frame(game, runtime::CaptureRun::FrameSeconds);
      status = run.step(target);
    }
    // No statement after a FAIL: MSVC reads it as unreachable, which is an error there.
    REQUIRE(status != runtime::CaptureRun::Status::Running); // finished within 600 frames
    return status;
  }
};

// Where the captures below write their relative outputs. The player resolves them under its
// preferences directory; the tests use the per-process temporary directory instead, so a run leaves
// the user's data directory alone and two runs at once do not share files (issue #405).
[[nodiscard]] std::filesystem::path captureDirectory() {
  return std::filesystem::temp_directory_path() / "runtime_tests";
}

// The player's capture flags, parsed as the player parses them, with the output resolved as it
// resolves it, under captureDirectory() rather than the preferences directory.
[[nodiscard]] runtime::CaptureOptions playerCapture(std::initializer_list<std::string_view> flags) {
  const std::vector<std::string_view> args{flags};
  const auto line = runtime::parseCommandLine(args, runtime::CaptureApplication::Player);
  REQUIRE(line.has_value());
  REQUIRE(line->capture.has_value());
  runtime::CaptureOptions options = *line->capture;
  runtime::resolveOutputs(options, captureDirectory());
  return options;
}

// Keeps what the scripts logged at info and above.
class MessageSink final : public spdlog::sinks::base_sink<std::mutex> {
public:
  std::vector<std::string> messages;

protected:
  void sink_it_(const spdlog::details::log_msg &message) override {
    if (message.level >= spdlog::level::info) {
      messages.emplace_back(message.payload.data(), message.payload.size());
    }
  }
  void flush_() override {
  }
};

struct Png {
  int width{0};
  int height{0};
  std::vector<unsigned char> rgba;
};

[[nodiscard]] Png readPng(const std::filesystem::path &file) {
  const auto bytes = core::readFile(file);
  REQUIRE(bytes.has_value());
  Png png;
  int channels = 0;
  unsigned char *pixels = stbi_load_from_memory(reinterpret_cast<const unsigned char *>(bytes->data()),
                                                static_cast<int>(bytes->size()), &png.width, &png.height, &channels, 4);
  REQUIRE(pixels != nullptr);
  png.rgba.assign(pixels, pixels + static_cast<std::size_t>(png.width) * static_cast<std::size_t>(png.height) * 4);
  stbi_image_free(pixels);
  return png;
}

} // namespace

TEST_CASE("the game UI is drawn over the scene and in the screenshot", "[runtime][gui][gpu]") {
  Fixture fixture;
  runtime::Game game{*fixture.window, *fixture.device, *fixture.swapchain, fixture.desc()};
  const flecs::entity canvas = game.world().createEntity("Canvas");
  canvas.set<gui::GuiCanvas>({.referenceSize = {640.0f, 480.0f}});
  const flecs::entity box = game.world().createEntity("Box", canvas);
  box.set<gui::GuiRect>({.anchorMin = {0.0f, 0.0f},
                         .anchorMax = {0.0f, 0.0f},
                         .pivot = {0.5f, 0.5f},
                         .offsetMin = {0.0f, 0.0f},
                         .offsetMax = {100.0f, 100.0f}});
  box.set<gui::GuiImage>({.tint = {1.0f, 0.0f, 0.0f, 1.0f}});

  const std::filesystem::path written = captureDirectory() / "shots" / "gui.png";
  std::filesystem::remove(written);
  game.requestScreenshot(written);
  fixture.frame(game);
  REQUIRE(
      std::ranges::any_of(game.graph().statistics().passes, [](const auto &pass) { return pass.name == "overlay"; }));
  const auto result = game.takeScreenshotResult();
  REQUIRE((result.has_value() && result->has_value()));

  // Opaque red at the top-left, where the box is, and not at the other corner.
  const Png png = readPng(written);
  const auto at = [&](int x, int y) {
    return &png.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(png.width) + static_cast<std::size_t>(x)) *
                     4];
  };
  REQUIRE((at(50, 50)[0] > 250 && at(50, 50)[1] < 5 && at(50, 50)[2] < 5));
  REQUIRE(at(png.width - 50, png.height - 50)[0] < 200);
  std::filesystem::remove(written);

  // The same scene without the UI declares no overlay pass.
  game.world().destroyEntity(canvas);
  fixture.frame(game);
  REQUIRE(
      std::ranges::none_of(game.graph().statistics().passes, [](const auto &pass) { return pass.name == "overlay"; }));
}

TEST_CASE("a touch on a button is the UI's and a touch elsewhere is the game's", "[runtime][gpu]") {
  Fixture fixture;
  runtime::Game game{*fixture.window, *fixture.device, *fixture.swapchain, fixture.desc()};
  const flecs::entity canvas = game.world().createEntity("Canvas");
  canvas.set<gui::GuiCanvas>({.referenceSize = {640.0f, 480.0f}});
  const flecs::entity button = game.world().createEntity("Button", canvas);
  button.set<gui::GuiRect>({.anchorMin = {0.0f, 0.0f},
                            .anchorMax = {0.0f, 0.0f},
                            .pivot = {0.5f, 0.5f},
                            .offsetMin = {0.0f, 0.0f},
                            .offsetMax = {100.0f, 100.0f}});
  button.set<gui::GuiButton>({});
  fixture.frame(game); // lays the canvas out

  game.event(platform::TouchDown{.id = 1, .position = {50.0f, 50.0f}});
  game.event(platform::TouchDown{.id = 2, .position = {400.0f, 300.0f}});
  REQUIRE(game.input().touches().size() == 1);
  REQUIRE(game.input().touches()[0].id == 2);
  REQUIRE(game.gui().events().size() == 1);
  fixture.frame(game);
  REQUIRE(game.gui().events().empty()); // delivered, then forgotten
}

TEST_CASE("a script's game.quit reaches the application through Game", "[runtime][gpu]") {
  Fixture fixture;
  runtime::Game game{*fixture.window, *fixture.device, *fixture.swapchain, fixture.desc()};
  REQUIRE_FALSE(game.takeQuitRequest());
  REQUIRE(game.scripts().run("game.quit()", "quit").has_value());
  fixture.frame(game);
  REQUIRE(game.takeQuitRequest() == platform::canQuit());
  REQUIRE_FALSE(game.takeQuitRequest());
}

TEST_CASE("the player runs a project folder's start scene", "[runtime][gpu]") {
  Fixture fixture;
  const std::filesystem::path project = sampleProject(fixture.platform);
  if (project.empty()) {
    SKIP("the basic sample was not found in a checkout above the test binary");
  }

  const auto sink = std::make_shared<ProblemSink>();
  core::Log::addSink(sink);
  runtime::Game game{*fixture.window, *fixture.device, *fixture.swapchain, fixture.desc()};
  REQUIRE(game.open(project).has_value());
  REQUIRE(game.name() == "Basic");
  REQUIRE(game.world().isPlaying()); // a player has no edit mode
  REQUIRE(fixture.window->title() == "Basic");

  // The scene's own camera, not the fallback, and its entities are there.
  REQUIRE(world::sceneCamera(game.world()).has_value());
  const std::size_t entities = game.world().roots().size();
  REQUIRE(entities > 0);

  sink->problems.clear(); // loading warnings are not this test's business
  for (int i = 0; i < 8; ++i) {
    fixture.frame(game);
  }
  // The scene passes ran and the copy into the swapchain image with them.
  const auto passes = game.graph().statistics().passes;
  REQUIRE(std::ranges::any_of(passes, [](const auto &pass) { return pass.name == "forward"; }));
  REQUIRE(std::ranges::any_of(passes, [](const auto &pass) { return pass.name == "present"; }));
  REQUIRE(sink->problems.empty()); // no validation message and nothing warned about
  core::Log::removeSink(sink);
}

TEST_CASE("the generic player refuses a project or a bundle that needs a game module", "[runtime][module][gpu]") {
  Fixture fixture;
  const std::filesystem::path project = sampleProject(fixture.platform, "hook");
  if (project.empty()) {
    SKIP("the hook sample was not found in a checkout above the test binary");
  }
  runtime::Game game{*fixture.window, *fixture.device, *fixture.swapchain, fixture.desc()};

  // Refused before anything loads (ADR-0027): the scene's Orbit would otherwise be dropped.
  const auto opened = game.open(project);
  REQUIRE(!opened.has_value());
  REQUIRE(opened.error().message ==
          "project \"Hook\" needs the game module \"hook\"; this player was built without one");
  REQUIRE(game.world().roots().empty());

  // A bundle records the module its project needs, and is refused the same way.
  const std::filesystem::path bundle = std::filesystem::temp_directory_path() / "sonnet_runtime_module.sbundle";
  {
    auto writer = assets::BundleWriter::create(bundle, {.name = "Hook",
                                                        .engineVersion = {},
                                                        .platform = assets::CookPlatform::Linux,
                                                        .startScene = "scenes/main.scene.json",
                                                        .module = "hook"});
    REQUIRE(writer.has_value());
    REQUIRE(writer->addFile("scenes/main.scene.json", assets::encodeJson({{"version", 3}})).has_value());
    REQUIRE(writer->finish().has_value());
  }
  const auto cooked = game.open(bundle);
  REQUIRE(!cooked.has_value());
  REQUIRE(cooked.error().message ==
          "bundle \"Hook\" needs the game module \"hook\"; this player was built without one");
  REQUIRE(game.assets().bundle() == nullptr); // closed again, so the file can go
  std::filesystem::remove(bundle);
}

TEST_CASE("the player runs the same project cooked into a bundle", "[runtime][gpu]") {
  Fixture fixture;
  const std::filesystem::path project = sampleProject(fixture.platform);
  if (project.empty()) {
    SKIP("the basic sample was not found in a checkout above the test binary");
  }
  const std::filesystem::path out = std::filesystem::temp_directory_path() / "sonnet_runtime_bundle";
  std::filesystem::remove_all(out);

  std::size_t projectEntities = 0;
  {
    // Cooked through a Game, so the cook and the run use one database as they do in the editor.
    runtime::Game game{*fixture.window, *fixture.device, *fixture.swapchain, fixture.desc()};
    REQUIRE(game.open(project).has_value());
    projectEntities = game.world().roots().size();
    const auto opened = assets::Project::open(project);
    REQUIRE(opened.has_value());
    const auto report = assets::cook(game.assets(), *opened, {.outputDirectory = out});
    REQUIRE(report.has_value());
    REQUIRE(report->warnings.empty());
  }

  const auto sink = std::make_shared<ProblemSink>();
  core::Log::addSink(sink);
  {
    runtime::Game game{*fixture.window, *fixture.device, *fixture.swapchain, fixture.desc()};
    REQUIRE(game.open(out / "game.sbundle").has_value());
    REQUIRE(game.name() == "Basic");
    REQUIRE(game.assets().bundle() != nullptr);
    // The same scene, from cooked assets: the entities the project had, and no hot reload.
    REQUIRE(game.world().roots().size() == projectEntities);
    REQUIRE(world::sceneCamera(game.world()).has_value());

    sink->problems.clear();
    for (int i = 0; i < 8; ++i) {
      fixture.frame(game);
    }
  }
  // Checked past the game's destruction: a cooked asset has no file record to unload through, so
  // a bundle that released nothing would show up here as the renderer's leak warnings.
  REQUIRE(sink->problems.empty());
  core::Log::removeSink(sink);
  sonnet::test::removeScratch(out);
}

TEST_CASE("a scene without a camera is drawn from the fallback view", "[runtime][gpu]") {
  Fixture fixture;
  const std::filesystem::path root = std::filesystem::temp_directory_path() / "sonnet_runtime_nocamera";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "scenes");
  assets::Project project;
  project.root = root;
  project.name = "No camera";
  project.assetRoots = {};
  REQUIRE(project.save().has_value());
  REQUIRE(core::writeFile(root / "scenes" / "main.scene.json", std::string_view{R"({"version": 2, "entities": []})"})
              .has_value());

  runtime::Game game{*fixture.window, *fixture.device, *fixture.swapchain, fixture.desc()};
  REQUIRE(game.open(root).has_value());
  fixture.frame(game);
  // The fallback looks at the origin from up and to the side, so it is not the identity.
  REQUIRE(game.camera().position.y > 0.0f);
  fixture.frame(game); // warned once, not every frame
  sonnet::test::removeScratch(root);
}

// Issue #515: the camera handed to the renderer is the one this frame's update placed, not the one
// the frame before left, so a camera a script moves is drawn with the transforms it is drawn among.
TEST_CASE("the drawn camera is where this frame's scripts moved it", "[runtime][gpu]") {
  Fixture fixture;
  const std::filesystem::path root = std::filesystem::temp_directory_path() / "sonnet_runtime_camera_after_update";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "assets");
  std::filesystem::create_directories(root / "scenes");
  assets::Project project;
  project.root = root;
  project.name = "Dolly";
  project.assetRoots = {"assets"};
  REQUIRE(project.save().has_value());
  REQUIRE(core::writeFile(root / "assets" / "dolly.lua", std::string_view{R"(
local Dolly = {}
function Dolly:update(dt)
  local transform = self.entity:get("Transform")
  transform.position = transform.position + vec3(1, 0, 0)
  self.entity:set("Transform", transform)
end
return Dolly
)"})
              .has_value());
  REQUIRE(core::writeFile(root / "assets" / "dolly.lua.meta",
                          std::string_view{R"({"version": 2, "uuid": "8d2f5c10-7a3e-4b6d-9c1f-515000000002",
                                            "type": "Script", "settings": {}})"})
              .has_value());
  REQUIRE(core::writeFile(root / "scenes" / "main.scene.json", std::string_view{R"({"version": 3, "entities": [
    {"uuid": "8d2f5c10-7a3e-4b6d-9c1f-515000000010", "name": "Camera",
     "components": {
       "Transform": {"position": [0, 1, 5], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1]},
       "Camera": {},
       "Scripts": {"slots": [{"script": "8d2f5c10-7a3e-4b6d-9c1f-515000000002", "properties": {}}]}}}
  ]})"})
              .has_value());

  runtime::Game game{*fixture.window, *fixture.device, *fixture.swapchain, fixture.desc()};
  REQUIRE(game.open(root).has_value());
  // Updates alone: the camera handed to the renderer is the view's, built at the end of update.
  for (int frame = 1; frame <= 4; ++frame) {
    game.update(1.0f / 60.0f);
    REQUIRE(game.view().camera.position.x == Approx(static_cast<float>(frame)));
    REQUIRE(game.view().camera.position.y == Approx(1.0f));
    REQUIRE(game.view().camera.position.z == Approx(5.0f));
  }
  sonnet::test::removeScratch(root);
}

// What the player does on WillEnterBackground and DidEnterForeground (docs/player.md, "The
// lifecycle"), which lives in apps/player/main.cpp: the game goes on without an image and without
// sound, and draws again once both are back.
TEST_CASE("the game survives its swapchain suspended and its audio paused", "[runtime][gpu]") {
  Fixture fixture;
  const std::filesystem::path project = sampleProject(fixture.platform);
  if (project.empty()) {
    SKIP("the basic sample was not found in a checkout above the test binary");
  }
  runtime::Game game{*fixture.window, *fixture.device, *fixture.swapchain, fixture.desc()};
  REQUIRE(game.open(project).has_value());
  const auto sink = std::make_shared<ProblemSink>();
  core::Log::addSink(sink);
  for (int i = 0; i < 3; ++i) {
    fixture.frame(game);
  }

  fixture.device->waitIdle();
  fixture.swapchain->suspend();
  game.audio().pause();
  const auto frames = game.world().ecs().get_info()->frame_count_total;
  for (int i = 0; i < 3; ++i) {
    game.update(1.0f / 60.0f);
    rhi::ICommandList &commands = fixture.device->beginFrame();
    const auto image = fixture.swapchain->acquire();
    REQUIRE_FALSE(image.has_value());
    game.render(commands, image);
    fixture.device->endFrame();
    REQUIRE(game.audio().lastMix().empty());
  }
  // The world ran its frames; only the drawing and the sound stopped.
  REQUIRE(game.world().ecs().get_info()->frame_count_total == frames + 3);

  REQUIRE(fixture.swapchain->resume().has_value());
  game.audio().resume();
  REQUIRE(fixture.swapchain->extent() == glm::uvec2{640, 480});
  for (int i = 0; i < 3; ++i) {
    fixture.frame(game);
  }
  const auto passes = game.graph().statistics().passes;
  REQUIRE(std::ranges::any_of(passes, [](const auto &pass) { return pass.name == "present"; }));
  REQUIRE_FALSE(game.audio().lastMix().empty());
  REQUIRE(sink->problems.empty());
  core::Log::removeSink(sink);
}

// ADR-0018's "CI": the player's capture flags, headless, on the path apps/player/main.cpp takes
// through runtime. The main.cpp around it, the window and the log's last line, no test reaches.
TEST_CASE("a player's capture run writes the scene under the preferences directory", "[runtime][capture][gpu]") {
  Fixture fixture;
  const std::filesystem::path project = sampleProject(fixture.platform);
  if (project.empty()) {
    SKIP("the basic sample was not found in a checkout above the test binary");
  }
  const runtime::CaptureOptions options = playerCapture(
      {"--play", "0.25", "--shading-term", "albedo", "--settle-frames", "2", "--screenshot", "shots/albedo.png"});
  const std::filesystem::path written = captureDirectory() / "shots" / "albedo.png";
  REQUIRE(options.viewport == written);
  std::filesystem::remove(written);

  const auto sink = std::make_shared<ProblemSink>();
  core::Log::addSink(sink);
  {
    runtime::GameDesc desc = fixture.desc();
    desc.paused = true;
    runtime::Game game{*fixture.window, *fixture.device, *fixture.swapchain, desc};
    REQUIRE(game.open(project).has_value());
    // Nothing simulates before the settle frames are drawn, as in the editor before it plays.
    REQUIRE_FALSE(game.world().isPlaying());
    runtime::CaptureRun run{options};
    REQUIRE(fixture.capture(game, run) == runtime::CaptureRun::Status::Done);
    REQUIRE(game.world().isPlaying()); // captured while playing, as asked
    REQUIRE(game.graph().statistics().passes.size() > 2);
  }
  REQUIRE(sink->problems.empty());
  core::Log::removeSink(sink);

  // The window's size, and something lit in the middle rather than the clear colour.
  const Png png = readPng(written);
  REQUIRE(png.width == static_cast<int>(fixture.swapchain->extent().x));
  REQUIRE(png.height == static_cast<int>(fixture.swapchain->extent().y));
  const std::size_t centre = (static_cast<std::size_t>(png.height / 2) * static_cast<std::size_t>(png.width) +
                              static_cast<std::size_t>(png.width / 2)) *
                             4;
  REQUIRE(png.rgba[centre] + png.rgba[centre + 1] + png.rgba[centre + 2] > 30);
  std::filesystem::remove(written);
}

TEST_CASE("a capture with --render-size draws and writes at that size, not the window's", "[runtime][capture][gpu]") {
  Fixture fixture;
  const std::filesystem::path project = sampleProject(fixture.platform);
  if (project.empty()) {
    SKIP("the basic sample was not found in a checkout above the test binary");
  }
  const runtime::CaptureOptions options =
      playerCapture({"--settle-frames", "2", "--render-size", "96x64", "--screenshot", "shots/sized.png"});
  REQUIRE(options.renderSize == glm::uvec2{96, 64});
  const std::filesystem::path written = captureDirectory() / "shots" / "sized.png";
  std::filesystem::remove(written);
  {
    runtime::GameDesc desc = fixture.desc();
    desc.paused = true;
    runtime::Game game{*fixture.window, *fixture.device, *fixture.swapchain, desc};
    REQUIRE(game.open(project).has_value());
    runtime::CaptureRun run{options};
    REQUIRE(fixture.capture(game, run) == runtime::CaptureRun::Status::Done);
    REQUIRE(game.target().size() == glm::uvec2{96, 64});
  }
  const Png png = readPng(written);
  REQUIRE(png.width == 96);
  REQUIRE(png.height == 64);
  REQUIRE(fixture.swapchain->extent() != glm::uvec2{96, 64}); // the window kept its own size
  std::filesystem::remove(written);
}

TEST_CASE("--scene opens another of a bundle's scenes for the capture", "[runtime][capture][gpu]") {
  Fixture fixture;
  const std::filesystem::path project = sampleProject(fixture.platform);
  if (project.empty()) {
    SKIP("the basic sample was not found in a checkout above the test binary");
  }
  const std::filesystem::path out = std::filesystem::temp_directory_path() / "sonnet_runtime_capture_bundle";
  std::filesystem::remove_all(out);
  {
    runtime::Game game{*fixture.window, *fixture.device, *fixture.swapchain, fixture.desc()};
    REQUIRE(game.open(project).has_value());
    const auto opened = assets::Project::open(project);
    REQUIRE(opened.has_value());
    REQUIRE(assets::cook(game.assets(), *opened, {.outputDirectory = out}).has_value());
  }
  const std::filesystem::path shot = out / "playground.png"; // absolute, so it stays where it is

  runtime::GameDesc desc = fixture.desc();
  desc.paused = true;
  {
    runtime::Game game{*fixture.window, *fixture.device, *fixture.swapchain, desc};
    REQUIRE(game.open(out / "game.sbundle").has_value());
    const auto hasBall = [&] {
      return std::ranges::any_of(game.world().roots(), [](const flecs::entity root) {
        const world::Name *name = root.try_get<world::Name>();
        return name != nullptr && name->value == "Ball";
      });
    };
    REQUIRE_FALSE(hasBall()); // the start scene is the main one
    runtime::CaptureRun run{playerCapture(
        {"--scene", "scenes/playground.scene.json", "--settle-frames", "1", "--screenshot", shot.string()})};
    REQUIRE(fixture.capture(game, run) == runtime::CaptureRun::Status::Done);
    REQUIRE(hasBall());
    REQUIRE_FALSE(game.world().isPlaying()); // no --play: the scene as it loads, as in the editor
  }
  REQUIRE(std::filesystem::is_regular_file(shot));

  // A scene the bundle does not hold fails the run, with nothing written.
  {
    runtime::Game game{*fixture.window, *fixture.device, *fixture.swapchain, desc};
    REQUIRE(game.open(out / "game.sbundle").has_value());
    runtime::CaptureRun run{
        playerCapture({"--scene", "scenes/nowhere.scene.json", "--screenshot", (out / "no.png").string()})};
    REQUIRE(fixture.capture(game, run) == runtime::CaptureRun::Status::Failed);
  }
  REQUIRE_FALSE(std::filesystem::exists(out / "no.png"));
  sonnet::test::removeScratch(out);
}

TEST_CASE("a path that is neither a project nor a bundle is an error", "[runtime][gpu]") {
  Fixture fixture;
  runtime::Game game{*fixture.window, *fixture.device, *fixture.swapchain, fixture.desc()};
  REQUIRE(!game.open(std::filesystem::temp_directory_path() / "sonnet_runtime_nowhere").has_value());
  REQUIRE(!game.open(std::filesystem::temp_directory_path() / "sonnet_runtime_nowhere.sbundle").has_value());
  // A failed open still leaves a game that renders an empty frame rather than crashing.
  fixture.frame(game);
}

// The null device reports what a desktop GPU does, BC and no ASTC, so a phone's bundle is refused
// on it and the same project cooked for Linux is not (docs/player.md, "Opening a game").
TEST_CASE("a bundle cooked for a phone is refused on a device without ASTC", "[runtime]") {
  platform::Platform platform{{.headless = true}};
  // SDL's windows are Vulkan windows, so even the null device's needs a Vulkan loader with surfaces.
  std::unique_ptr<platform::IWindow> window;
  try {
    window = platform.createWindow({.title = "runtime_tests", .size = {64, 64}});
  } catch (const core::Exception &e) {
    SKIP("no window: " << e.what());
  }
  const auto device = rhi::createNullDevice();
  REQUIRE_FALSE(device->info().astcSupported);
  const auto swapchain = device->createSwapchain(*window);
  const std::filesystem::path root = std::filesystem::temp_directory_path() / "sonnet_runtime_mobile";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "scenes");
  assets::Project project;
  project.root = root;
  project.name = "Phone";
  project.assetRoots = {};
  REQUIRE(project.save().has_value());
  REQUIRE(core::writeFile(root / "scenes" / "main.scene.json", std::string_view{R"({"version": 2, "entities": []})"})
              .has_value());

  runtime::GameDesc desc;
  desc.audioOutput = false;
  {
    runtime::Game game{*window, *device, *swapchain, desc};
    REQUIRE(game.open(root).has_value());
    for (const assets::CookPlatform target :
         {assets::CookPlatform::Android, assets::CookPlatform::IOS, assets::CookPlatform::Linux}) {
      REQUIRE(
          assets::cook(game.assets(), project,
                       {.outputDirectory = root / "export" / std::string{assets::toString(target)}, .platform = target})
              .has_value());
    }

    for (const char *mobile : {"android", "ios"}) {
      const auto opened = game.open(root / "export" / mobile / "game.sbundle");
      REQUIRE_FALSE(opened.has_value());
      REQUIRE(opened.error().message.contains(std::format("cooked for {}", mobile)));
      REQUIRE_FALSE(game.assets().isOpen()); // nothing of it stays loaded
    }
    REQUIRE(game.open(root / "export" / "linux" / "game.sbundle").has_value());
    REQUIRE(game.name() == "Phone");
  } // closes the bundle before the cleanup removes it; Windows refuses to delete an open file
  sonnet::test::removeScratch(root);
}

// M11's criterion: the playground's trigger-driven pickup plays the same from the project folder
// and from the cooked bundle, since the scene's version 3 `Scripts` and its properties reach the
// bundle as they are (ADR-0022, ADR-0011).
TEST_CASE("the playground's pickup is collected the same from a project and from a bundle", "[runtime][samples][gpu]") {
  Fixture fixture;
  const std::filesystem::path project = sampleProject(fixture.platform);
  if (project.empty()) {
    SKIP("the basic sample was not found in a checkout above the test binary");
  }
  const std::filesystem::path out = std::filesystem::temp_directory_path() / "sonnet_runtime_pickup";
  std::filesystem::remove_all(out);

  const auto hasRoot = [](runtime::Game &game, std::string_view name) {
    return std::ranges::any_of(game.world().roots(), [&](const flecs::entity root) {
      const world::Name *entity = root.try_get<world::Name>();
      return entity != nullptr && entity->value == name;
    });
  };
  // Four seconds of the playground: the first crate to land under the spawner takes "Crate coin"
  // and the ball's coin, which nothing touches, stays.
  const auto play = [&](const std::filesystem::path &source) {
    const auto problems = std::make_shared<ProblemSink>();
    const auto messages = std::make_shared<MessageSink>();
    core::Log::addSink(problems);
    core::Log::addSink(messages);
    runtime::Game game{*fixture.window, *fixture.device, *fixture.swapchain, fixture.desc()};
    REQUIRE(game.open(source).has_value());
    REQUIRE(game.openScene("scenes/playground.scene.json").has_value());
    REQUIRE(hasRoot(game, "Crate coin"));
    problems->problems.clear();
    for (int i = 0; i < 240; ++i) {
      game.update(1.0f / 60.0f);
    }
    const bool crateCoin = hasRoot(game, "Crate coin");
    const bool coin = hasRoot(game, "Coin");
    core::Log::removeSink(messages);
    core::Log::removeSink(problems);
    REQUIRE(problems->problems.empty());
    REQUIRE(!crateCoin);
    REQUIRE(coin);
    return messages->messages;
  };

  {
    runtime::Game game{*fixture.window, *fixture.device, *fixture.swapchain, fixture.desc()};
    REQUIRE(game.open(project).has_value());
    const auto opened = assets::Project::open(project);
    REQUIRE(opened.has_value());
    REQUIRE(assets::cook(game.assets(), *opened, {.outputDirectory = out}).has_value());
  }
  const auto picked = [](const std::vector<std::string> &messages) {
    return static_cast<int>(std::ranges::count_if(
        messages, [](const std::string &line) { return line == "Physics crate picked up Crate coin: score 5"; }));
  };
  REQUIRE(picked(play(project)) == 1);
  REQUIRE(picked(play(out / "game.sbundle")) == 1);
  sonnet::test::removeScratch(out);
}

// M16's criterion (ADR-0028): the playground places a pallet, a prefab holding three instances of
// the crate prefab and itself changing Crate B's lid. The scene turns Crate A's lid red, adds a
// lantern under Crate B and hides Crate C, and all of it comes back from the project and from
// the bundle, and saves again under the same derived identities.
TEST_CASE("the playground's pallet of crates keeps its changes from a project and from a bundle",
          "[runtime][samples][gpu][nested]") {
  Fixture fixture;
  const std::filesystem::path project = sampleProject(fixture.platform);
  if (project.empty()) {
    SKIP("the basic sample was not found in a checkout above the test binary");
  }
  const std::filesystem::path out = std::filesystem::temp_directory_path() / "sonnet_runtime_pallet";
  std::filesystem::remove_all(out);
  const auto file = core::readFile(project / "scenes" / "playground.scene.json");
  REQUIRE(file.has_value());
  const nlohmann::json authored = assets::parseJson(*file);
  // The identities a save writes for what the scene changed inside its instances.
  const auto changed = [](const nlohmann::json &scene) {
    std::vector<std::string> uuids;
    for (const nlohmann::json &entry : scene["entities"]) {
      if (entry.value("instanced", false) || entry.value("name", std::string{}) == "Lantern") {
        uuids.push_back(entry["uuid"].get<std::string>() + entry.value("parent", std::string{}));
      }
    }
    std::ranges::sort(uuids);
    return uuids;
  };
  const auto check = [&](const std::filesystem::path &source) {
    runtime::Game game{*fixture.window, *fixture.device, *fixture.swapchain, fixture.desc()};
    REQUIRE(game.open(source).has_value());
    REQUIRE(game.openScene("scenes/playground.scene.json").has_value());
    world::World &world = game.world();
    flecs::entity pallet;
    for (const flecs::entity root : world.roots()) {
      if (root.get<world::Name>().value == "Pallet") {
        pallet = root;
      }
    }
    REQUIRE(world.isInstance(pallet));
    REQUIRE(world.findByPath(pallet, "Crate A/Lid").get<world::MeshRenderer>().color.r == Approx(0.9f));
    REQUIRE(world.findByPath(pallet, "Crate B/Lid").get<world::MeshRenderer>().color.b == Approx(0.9f));
    REQUIRE(world.findByPath(pallet, "Crate C/Lid").get<world::MeshRenderer>().color.b == Approx(0.3f));
    REQUIRE(world.findByPath(pallet, "Crate B/Lantern"));
    REQUIRE(world.findByPath(pallet, "Crate C").has<world::Disabled>());
    REQUIRE(!world.findByPath(pallet, "Crate A").has<world::Disabled>());
    const nlohmann::json saved = world::saveScene(world);
    REQUIRE(changed(saved) == changed(authored));
    // A lid stores its colour alone, so its other members follow the crate prefab (ADR-0037).
    for (const nlohmann::json &entry : saved["entities"]) {
      if (entry.value("instanced", false) && entry["components"].contains("MeshRenderer")) {
        REQUIRE(entry["components"]["MeshRenderer"].size() == 1);
        REQUIRE(entry["components"]["MeshRenderer"].contains("color"));
      }
    }
    game.update(1.0f / 60.0f);
    // Hiding Crate C hides its lid too, which only the crate's own tag says.
    std::vector<renderer::DrawItem> draws;
    std::vector<glm::mat4> joints;
    std::vector<float> morphWeights;
    world::buildDrawList(world, game.assets(), draws, joints, morphWeights);
    const auto drawn = [&](std::string_view path) {
      return std::ranges::contains(draws, world::World::pickId(world.findByPath(pallet, path)),
                                   &renderer::DrawItem::id);
    };
    REQUIRE(drawn("Crate A/Lid"));
    REQUIRE(!drawn("Crate C"));
    REQUIRE(!drawn("Crate C/Lid"));
  };
  {
    runtime::Game game{*fixture.window, *fixture.device, *fixture.swapchain, fixture.desc()};
    REQUIRE(game.open(project).has_value());
    const auto opened = assets::Project::open(project);
    REQUIRE(opened.has_value());
    REQUIRE(assets::cook(game.assets(), *opened, {.outputDirectory = out}).has_value());
  }
  REQUIRE(changed(authored).size() == 3);
  check(project);
  check(out / "game.sbundle");
  sonnet::test::removeScratch(out);
}

// M12's criterion: the start scene's walker crossfades from idle to walk and hears its footsteps,
// and its jelly wobbles through a morph target, the same from a project and from a bundle, since
// clips, events and morph targets reach the bundle in its payloads (ADR-0023).
TEST_CASE("the start scene's walker, jelly and sparks play the same from a project and a bundle",
          "[runtime][samples][gpu]") {
  Fixture fixture;
  const std::filesystem::path project = sampleProject(fixture.platform);
  if (project.empty()) {
    SKIP("the basic sample was not found in a checkout above the test binary");
  }
  const std::filesystem::path out = std::filesystem::temp_directory_path() / "sonnet_runtime_effects";
  std::filesystem::remove_all(out);
  const auto root = [](runtime::Game &game, std::string_view name) {
    const auto roots = game.world().roots();
    const auto it = std::ranges::find_if(roots, [&](const flecs::entity entity) {
      const world::Name *named = entity.try_get<world::Name>();
      return named != nullptr && named->value == name;
    });
    REQUIRE(it != roots.end());
    return *it;
  };

  const auto play = [&](const std::filesystem::path &source) {
    const auto problems = std::make_shared<ProblemSink>();
    const auto messages = std::make_shared<MessageSink>();
    core::Log::addSink(problems);
    core::Log::addSink(messages);
    runtime::Game game{*fixture.window, *fixture.device, *fixture.swapchain, fixture.desc()};
    REQUIRE(game.open(source).has_value());
    const flecs::entity walker = root(game, "Walker");
    const flecs::entity jelly = root(game, "Jelly");
    root(game, "Sparks");
    const core::Uuid idle = walker.get<world::Animator>().clip;

    // The jelly's first second takes its weight from 0 to 1 through a Weights channel.
    problems->problems.clear();
    for (int i = 0; i < 60; ++i) {
      game.update(1.0f / 60.0f);
    }
    const flecs::entity face = game.world().findByPath(jelly, "Jelly");
    REQUIRE(face);
    REQUIRE(face.get<world::MorphWeights>().weights.at(0) > 0.9f);
    REQUIRE(walker.get<world::Animator>().clip == idle); // still standing

    // Three seconds in the walker crossfades to Walk, the idle clip fading out over 0.4 s in the
    // animation system's own state rather than in the script's layers (ADR-0038).
    for (int i = 0; i < 125; ++i) {
      game.update(1.0f / 60.0f);
    }
    REQUIRE(walker.get<world::Animator>().clip != idle);
    REQUIRE(walker.get<world::Animator>().layers.empty());
    for (int i = 0; i < 60; ++i) {
      game.update(1.0f / 60.0f);
    }
    core::Log::removeSink(messages);
    core::Log::removeSink(problems);
    REQUIRE(problems->problems.empty());
    return messages->messages;
  };

  {
    runtime::Game game{*fixture.window, *fixture.device, *fixture.swapchain, fixture.desc()};
    REQUIRE(game.open(project).has_value());
    const auto opened = assets::Project::open(project);
    REQUIRE(opened.has_value());
    REQUIRE(assets::cook(game.assets(), *opened, {.outputDirectory = out}).has_value());
  }
  const auto footsteps = [](const std::vector<std::string> &messages) {
    std::vector<std::string> steps;
    for (const std::string &line : messages) {
      if (line.starts_with("Walker: ") && line.contains(" footstep ")) {
        steps.push_back(line);
      }
    }
    return steps;
  };
  const std::vector<std::string> fromProject = footsteps(play(project));
  const std::vector<std::string> fromBundle = footsteps(play(out / "game.sbundle"));
  // Walk starts at 3 s and steps at a quarter and three quarters of every second, so by the
  // 4.08 s the test plays: left and right.
  REQUIRE(fromProject.size() == 2);
  REQUIRE(fromProject.front() == "Walker: left footstep 1");
  REQUIRE(fromBundle == fromProject);
  sonnet::test::removeScratch(out);
}

// ---- Golden images (ADR-0024) ----

namespace {

// The checkout's golden image directory, found the way the sample projects are; empty outside one.
[[nodiscard]] std::filesystem::path goldenDirectory(platform::Platform &platform) {
  std::error_code error;
  for (std::filesystem::path base = std::filesystem::absolute(platform.basePath(), error); !base.empty();
       base = base.parent_path()) {
    const std::filesystem::path candidate = base / "modules" / "runtime" / "tests" / "golden";
    if (std::filesystem::is_directory(candidate, error)) {
      return candidate.lexically_normal();
    }
    if (base == base.root_path()) {
      break;
    }
  }
  return {};
}

// The error image of two same-sized RGBA8 images: per-pixel the largest channel difference.
struct ErrorImage {
  double mean{0.0};         // of that, over every pixel, in levels of 255
  int maximum{0};           // the worst pixel
  double fractionOver{0.0}; // the share of pixels off by more than `threshold`
};

[[nodiscard]] ErrorImage compare(const Png &a, const Png &b, int threshold) {
  REQUIRE(a.width == b.width);
  REQUIRE(a.height == b.height);
  ErrorImage result;
  const std::size_t pixels = static_cast<std::size_t>(a.width) * static_cast<std::size_t>(a.height);
  double sum = 0.0;
  std::size_t over = 0;
  for (std::size_t i = 0; i < pixels; ++i) {
    int worst = 0;
    for (std::size_t channel = 0; channel < 3; ++channel) {
      worst = std::max(worst, std::abs(int{a.rgba[i * 4 + channel]} - int{b.rgba[i * 4 + channel]}));
    }
    sum += worst;
    result.maximum = std::max(result.maximum, worst);
    over += worst > threshold ? 1 : 0;
  }
  result.mean = sum / static_cast<double>(pixels);
  result.fractionOver = static_cast<double>(over) / static_cast<double>(pixels);
  return result;
}

// The showcase after a second of play, as the player captures it, under one anti-aliasing mode.
// A non-empty `keep` also copies the written file there.
[[nodiscard]] Png captureShowcase(Fixture &fixture, const std::filesystem::path &project, renderer::AntiAliasing mode,
                                  std::string_view name, const std::filesystem::path &keep = {}) {
  runtime::GameDesc desc = fixture.desc();
  desc.paused = true;
  desc.renderer.antialiasing = mode;
  desc.renderer.bloom = true;
  desc.renderer.bloomLevels = 3;
  const runtime::CaptureOptions options =
      playerCapture({"--play", "1", "--settle-frames", "12", "--screenshot", std::format("golden/{}.png", name)});
  std::filesystem::remove(options.viewport);
  {
    runtime::Game game{*fixture.window, *fixture.device, *fixture.swapchain, desc};
    REQUIRE(game.open(project).has_value());
    runtime::CaptureRun run{options};
    REQUIRE(fixture.capture(game, run) == runtime::CaptureRun::Status::Done);
  }
  Png png = readPng(options.viewport);
  if (!keep.empty()) {
    std::filesystem::copy_file(options.viewport, keep, std::filesystem::copy_options::overwrite_existing);
  }
  std::filesystem::remove(options.viewport);
  return png;
}

} // namespace

// Rendered on Lavapipe, where a golden image is reproducible; another driver rounds differently, so
// the case is skipped there rather than given a tolerance wide enough for everything. Set
// SONNET_UPDATE_GOLDEN to write the image instead of comparing with it.
TEST_CASE("the showcase resolves to its golden image, and to the unresolved frame without ghosting",
          "[runtime][capture][golden][gpu]") {
  Fixture fixture{{320, 240}};
  if (fixture.device->info().driverName != "llvmpipe") {
    SKIP("golden images are rendered on Lavapipe");
  }
  const std::filesystem::path project = sampleProject(fixture.platform, "showcase");
  const std::filesystem::path directory = goldenDirectory(fixture.platform);
  if (project.empty() || directory.empty()) {
    SKIP("the showcase sample and its golden images were not found in a checkout above the test binary");
  }
  const std::filesystem::path golden = directory / "showcase_taa.png";
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996) // getenv is the standard call; _dupenv_s is MSVC-only
#endif
  const bool update = std::getenv("SONNET_UPDATE_GOLDEN") != nullptr;
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
  const Png resolved = captureShowcase(fixture, project, renderer::AntiAliasing::Taa, "showcase_taa",
                                       update ? golden : std::filesystem::path{});
  REQUIRE(std::filesystem::is_regular_file(golden));
  const ErrorImage against = compare(resolved, readPng(golden), 32);
  INFO("against the golden: mean " << against.mean << ", max " << against.maximum
                                   << ", over 32: " << against.fractionOver);
  REQUIRE(against.mean <= 1.5);
  REQUIRE(against.maximum <= 96);
  REQUIRE(against.fractionOver <= 0.002);

  // Against the same frame with no anti-aliasing: smoother edges and softer texture detail (the
  // mean sits near 6.5 levels, spread evenly over the image), not smeared ones. A trail behind a
  // moving object would put wide bands of large errors in this image.
  const Png unresolved = captureShowcase(fixture, project, renderer::AntiAliasing::None, "showcase_none");
  const ErrorImage versus = compare(resolved, unresolved, 48);
  INFO("against no anti-aliasing: mean " << versus.mean << ", max " << versus.maximum
                                         << ", over 48: " << versus.fractionOver);
  REQUIRE(versus.mean <= 9.0);
  REQUIRE(versus.fractionOver <= 0.04);
}

namespace {

// The centre of a laid-out element, in the window's coordinates.
[[nodiscard]] glm::vec2 centreOf(runtime::Game &game, flecs::entity entity) {
  const gui::GuiElement *element = gui::findElement(game.gui().elements(), entity.id());
  REQUIRE(element != nullptr);
  return (element->rect.min + element->rect.max) * 0.5f;
}

void click(runtime::Game &game, glm::vec2 at) {
  game.event(platform::MouseButtonPressed{.button = platform::MouseButton::Left, .position = at, .clicks = 1});
  game.event(platform::MouseButtonReleased{.button = platform::MouseButton::Left, .position = at});
}

} // namespace

TEST_CASE("the basic sample's menu opens and closes its popup from clicks", "[runtime][samples][gui][gpu]") {
  Fixture fixture;
  const std::filesystem::path project = sampleProject(fixture.platform);
  if (project.empty()) {
    SKIP("the basic sample was not found in a checkout above the test binary");
  }
  runtime::Game game{*fixture.window, *fixture.device, *fixture.swapchain, fixture.desc()};
  REQUIRE(game.open(project).has_value());
  REQUIRE(game.openScene("scenes/menu.scene.json").has_value());
  // The first frames start the scripts and lay the canvas out.
  fixture.frame(game);
  fixture.frame(game);

  const auto find = [&](std::string_view path) {
    for (const flecs::entity root : game.world().roots()) {
      if (root.get<world::Name>().value == "Canvas") {
        const flecs::entity found = game.world().findByPath(root, path);
        REQUIRE(found.is_valid());
        return found;
      }
    }
    FAIL("the menu has no canvas");
  };
  const flecs::entity popup = find("OptionsPanel");
  const flecs::entity options = find("Menu/Buttons/Options");
  const flecs::entity quit = find("Menu/Buttons/Quit");
  REQUIRE(popup.has<world::Disabled>());

  // A click on Options opens the popup, and the game never sees it.
  click(game, centreOf(game, options));
  fixture.frame(game);
  REQUIRE_FALSE(popup.has<world::Disabled>());
  REQUIRE_FALSE(game.input().mousePressed(platform::MouseButton::Left));
  REQUIRE_FALSE(game.input().mouseDown(platform::MouseButton::Left));

  // The popup is laid out now; its Close button hides it again.
  fixture.frame(game);
  click(game, centreOf(game, find("OptionsPanel/Close")));
  fixture.frame(game);
  REQUIRE(popup.has<world::Disabled>());

  // Quit ends the game where the platform lets it, and is hidden where it does not.
  REQUIRE(quit.has<world::Disabled>() == !platform::canQuit());
  if (platform::canQuit()) {
    REQUIRE_FALSE(game.takeQuitRequest());
    click(game, centreOf(game, quit));
    fixture.frame(game);
    REQUIRE(game.takeQuitRequest());
    REQUIRE(popup.has<world::Disabled>());
  }
  // A click beside the menu lands on the dim layer, which blocks input without being a button.
  click(game, {5.0f, 5.0f});
  fixture.frame(game);
  REQUIRE_FALSE(game.input().mouseDown(platform::MouseButton::Left));
}

TEST_CASE("the basic sample's options popup opens inside a portrait window", "[runtime][samples][gui][gpu]") {
  // A phone held upright, the S25's 1080x2340 at a third of the size: the reference canvas is
  // landscape, so a popup placed beside the menu fell off the right edge there.
  Fixture fixture{{360, 780}};
  const std::filesystem::path project = sampleProject(fixture.platform);
  if (project.empty()) {
    SKIP("the basic sample was not found in a checkout above the test binary");
  }
  runtime::Game game{*fixture.window, *fixture.device, *fixture.swapchain, fixture.desc()};
  REQUIRE(game.open(project).has_value());
  REQUIRE(game.openScene("scenes/menu.scene.json").has_value());
  fixture.frame(game);
  fixture.frame(game);

  flecs::entity canvas;
  for (const flecs::entity root : game.world().roots()) {
    if (root.get<world::Name>().value == "Canvas") {
      canvas = root;
    }
  }
  REQUIRE(canvas.is_valid());
  click(game, centreOf(game, game.world().findByPath(canvas, "Menu/Buttons/Options")));
  fixture.frame(game);
  fixture.frame(game);

  const glm::vec2 window{fixture.window->size()};
  for (const char *path : {"OptionsPanel", "OptionsPanel/Close"}) {
    const gui::GuiElement *element =
        gui::findElement(game.gui().elements(), game.world().findByPath(canvas, path).id());
    REQUIRE(element != nullptr);
    INFO(path << " spans " << element->rect.min.x << "," << element->rect.min.y << " to " << element->rect.max.x << ","
              << element->rect.max.y << " in a " << window.x << "x" << window.y << " window");
    REQUIRE(element->rect.min.x >= 0.0f);
    REQUIRE(element->rect.min.y >= 0.0f);
    REQUIRE(element->rect.max.x <= window.x);
    REQUIRE(element->rect.max.y <= window.y);
  }
}

TEST_CASE("the basic sample plays from its menu, pauses and goes back", "[runtime][samples][gui][gpu]") {
  Fixture fixture;
  const std::filesystem::path project = sampleProject(fixture.platform);
  if (project.empty()) {
    SKIP("the basic sample was not found in a checkout above the test binary");
  }
  runtime::Game game{*fixture.window, *fixture.device, *fixture.swapchain, fixture.desc()};
  REQUIRE(game.open(project).has_value());
  REQUIRE(game.openScene("scenes/menu.scene.json").has_value());
  // The scene root of that name, or an invalid entity.
  const auto root = [&](std::string_view name) {
    for (const flecs::entity entity : game.world().roots()) {
      if (entity.get<world::Name>().value == name) {
        return entity;
      }
    }
    return flecs::entity{};
  };
  const auto find = [&](std::string_view canvas, std::string_view path) {
    REQUIRE(root(canvas).is_valid());
    const flecs::entity found = game.world().findByPath(root(canvas), path);
    REQUIRE(found.is_valid());
    return found;
  };
  // The elevator's script moves it by its dt every frame.
  const auto elevatorHeight = [&] { return root("Elevator").get<world::Transform>().position.y; };
  fixture.frame(game);
  fixture.frame(game);

  // Play loads the playground after the frame that heard the click, with its HUD laid out.
  click(game, centreOf(game, find("Canvas", "Menu/Buttons/Play")));
  fixture.frame(game);
  REQUIRE(root("Ball").is_valid());
  REQUIRE_FALSE(root("Canvas").is_valid());
  fixture.frame(game);
  const flecs::entity pauseMenu = find("HUD", "PauseMenu");
  REQUIRE(pauseMenu.has<world::Disabled>());
  REQUIRE(find("HUD", "Score").get<gui::GuiText>().text == "Score 0");

  // Pause stops the game's time and shows the menu, while the scripts still run.
  click(game, centreOf(game, find("HUD", "Pause")));
  fixture.frame(game);
  REQUIRE(game.world().timeScale() == 0.0f);
  REQUIRE_FALSE(pauseMenu.has<world::Disabled>());
  fixture.frame(game);
  const float paused = elevatorHeight();
  for (int i = 0; i < 5; ++i) {
    fixture.frame(game);
  }
  REQUIRE(elevatorHeight() == paused);

  // The Pause button is behind the menu's dim layer now; Resume is what lets the game go on.
  click(game, centreOf(game, find("HUD", "Pause")));
  fixture.frame(game);
  REQUIRE(game.world().timeScale() == 0.0f);
  click(game, centreOf(game, find("HUD", "PauseMenu/Panel/Buttons/Resume")));
  fixture.frame(game);
  REQUIRE(game.world().timeScale() == 1.0f);
  REQUIRE(pauseMenu.has<world::Disabled>());
  fixture.frame(game);
  REQUIRE(elevatorHeight() != paused);

  // Main menu from a pause goes back to the title, at real time again.
  click(game, centreOf(game, find("HUD", "Pause")));
  fixture.frame(game);
  fixture.frame(game);
  click(game, centreOf(game, find("HUD", "PauseMenu/Panel/Buttons/MainMenu")));
  fixture.frame(game);
  REQUIRE(root("Canvas").is_valid());
  REQUIRE_FALSE(root("Ball").is_valid());
  REQUIRE(game.world().timeScale() == 1.0f);
}

TEST_CASE("a scene a script asks for that does not load leaves the running one", "[runtime][gpu]") {
  Fixture fixture;
  const std::filesystem::path project = sampleProject(fixture.platform);
  if (project.empty()) {
    SKIP("the basic sample was not found in a checkout above the test binary");
  }
  runtime::Game game{*fixture.window, *fixture.device, *fixture.swapchain, fixture.desc()};
  REQUIRE(game.open(project).has_value());
  REQUIRE(game.openScene("scenes/menu.scene.json").has_value());
  fixture.frame(game);
  const std::size_t entities = game.world().roots().size();
  const auto sink = std::make_shared<ProblemSink>();
  core::Log::addSink(sink);
  REQUIRE(game.scripts().run(R"(game.loadScene("scenes/missing.scene.json"))", "missing").has_value());
  fixture.frame(game);
  core::Log::removeSink(sink);
  REQUIRE(game.world().roots().size() == entities);
  REQUIRE(sink->problems.size() == 1);
  REQUIRE(sink->problems.front().starts_with("loading scenes/missing.scene.json for a script"));
}

TEST_CASE("the basic sample's menu resolves to its golden image", "[runtime][capture][golden][gui][gpu]") {
  Fixture fixture{{320, 240}};
  if (fixture.device->info().driverName != "llvmpipe") {
    SKIP("golden images are rendered on Lavapipe");
  }
  const std::filesystem::path project = sampleProject(fixture.platform);
  const std::filesystem::path directory = goldenDirectory(fixture.platform);
  if (project.empty() || directory.empty()) {
    SKIP("the basic sample and its golden images were not found in a checkout above the test binary");
  }
  const std::filesystem::path golden = directory / "menu.png";
  const runtime::CaptureOptions options = playerCapture({"--settle-frames", "6", "--screenshot", "golden/menu.png"});
  std::filesystem::remove(options.viewport);
  runtime::GameDesc desc = fixture.desc();
  desc.renderer.antialiasing = renderer::AntiAliasing::None;
  {
    runtime::Game game{*fixture.window, *fixture.device, *fixture.swapchain, desc};
    REQUIRE(game.open(project).has_value());
    REQUIRE(game.openScene("scenes/menu.scene.json").has_value());
    runtime::CaptureRun run{options};
    REQUIRE(fixture.capture(game, run) == runtime::CaptureRun::Status::Done);
  }
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996) // getenv is the standard call; _dupenv_s is MSVC-only
#endif
  const bool update = std::getenv("SONNET_UPDATE_GOLDEN") != nullptr;
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
  if (update) {
    std::filesystem::copy_file(options.viewport, golden, std::filesystem::copy_options::overwrite_existing);
  }
  REQUIRE(std::filesystem::is_regular_file(golden));
  const ErrorImage against = compare(readPng(options.viewport), readPng(golden), 32);
  std::filesystem::remove(options.viewport);
  INFO("against the golden: mean " << against.mean << ", max " << against.maximum
                                   << ", over 32: " << against.fractionOver);
  REQUIRE(against.mean <= 1.5);
  REQUIRE(against.maximum <= 96);
  REQUIRE(against.fractionOver <= 0.002);
}

namespace {

// A project of one label whose script changes its text on the twelfth update, with the sample's font.
std::filesystem::path labelProject(const std::filesystem::path &sample) {
  const std::filesystem::path root = std::filesystem::temp_directory_path() / "sonnet_runtime_label";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "assets");
  std::filesystem::create_directories(root / "scenes");
  assets::Project project;
  project.root = root;
  project.name = "Label";
  project.assetRoots = {"assets"};
  REQUIRE(project.save().has_value());
  std::filesystem::copy_file(sample / "assets" / "fonts" / "NotoSans-Regular.ttf", root / "assets" / "font.ttf");
  REQUIRE(core::writeFile(root / "assets" / "font.ttf.meta",
                          std::string_view{R"({"version": 2, "uuid": "6f1c2a40-5b0e-4c1d-9a3e-2b7d9d51f001",
                                            "settings": {"preload": [[32, 126]]}})"})
              .has_value());
  REQUIRE(core::writeFile(root / "assets" / "label.lua", std::string_view{R"(
local Label = {}
function Label:start()
  self.frames = 0
end
function Label:update(dt)
  self.frames = self.frames + 1
  if self.frames == 12 then
    self.entity:set("GuiText", { text = "Score 12" })
  end
end
return Label
)"})
              .has_value());
  REQUIRE(core::writeFile(root / "assets" / "label.lua.meta",
                          std::string_view{R"({"version": 2, "uuid": "6f1c2a40-5b0e-4c1d-9a3e-2b7d9d51f002",
                                            "type": "Script", "settings": {}})"})
              .has_value());
  REQUIRE(core::writeFile(root / "scenes" / "main.scene.json", std::string_view{R"({"version": 3, "entities": [
    {"uuid": "6f1c2a40-5b0e-4c1d-9a3e-2b7d9d51f010", "name": "Canvas", "components": {"GuiCanvas": {}}},
    {"uuid": "6f1c2a40-5b0e-4c1d-9a3e-2b7d9d51f011", "name": "Label", "parent": "6f1c2a40-5b0e-4c1d-9a3e-2b7d9d51f010",
     "components": {
       "GuiRect": {"anchorMin": {"x": 0, "y": 0}, "anchorMax": {"x": 1, "y": 1}, "offsetMin": {"x": 0, "y": 0},
                   "offsetMax": {"x": 0, "y": 0}},
       "GuiText": {"font": "6f1c2a40-5b0e-4c1d-9a3e-2b7d9d51f001", "text": "Score 0", "size": 200,
                   "horizontal": "Center", "vertical": "Center"},
       "Scripts": {"slots": [{"script": "6f1c2a40-5b0e-4c1d-9a3e-2b7d9d51f002", "properties": {}}]}}}
  ]})"})
              .has_value());
  return root;
}

[[nodiscard]] std::size_t glyphQuads(runtime::Game &game) {
  return static_cast<std::size_t>(std::ranges::count_if(
      game.gui().quads(), [](const renderer::OverlayQuad &quad) { return quad.kind == renderer::OverlayKind::Glyph; }));
}

} // namespace

TEST_CASE("a script that sets a label's text changes the next captured frame", "[runtime][capture][gui][gpu]") {
  Fixture fixture{{320, 240}};
  const std::filesystem::path sample = sampleProject(fixture.platform);
  if (sample.empty()) {
    SKIP("the basic sample was not found in a checkout above the test binary");
  }
  const std::filesystem::path root = labelProject(sample);
  runtime::GameDesc desc = fixture.desc();
  desc.renderer.antialiasing = renderer::AntiAliasing::None;
  runtime::Game game{*fixture.window, *fixture.device, *fixture.swapchain, desc};
  REQUIRE(game.open(root).has_value());

  const auto shot = [&](std::string_view name) {
    const runtime::CaptureOptions options = playerCapture({"--settle-frames", "1", "--screenshot", name});
    runtime::CaptureRun run{options};
    REQUIRE(fixture.capture(game, run) == runtime::CaptureRun::Status::Done);
    return readPng(options.viewport);
  };
  fixture.frame(game);
  REQUIRE(glyphQuads(game) == 6);                     // "Score 0"
  const auto before = shot("shots/label_before.png"); // a few frames, well short of the script's twelfth

  flecs::entity label;
  for (const flecs::entity canvas : game.world().roots()) {
    if (canvas.get<world::Name>().value == "Canvas") {
      label = game.world().findByPath(canvas, "Label");
    }
  }
  REQUIRE(label.is_valid());
  int frames = 0;
  while (label.get<gui::GuiText>().text != "Score 12" && frames++ < 40) {
    fixture.frame(game);
  }
  REQUIRE(label.get<gui::GuiText>().text == "Score 12");
  // The update that set the text and the render after it were one frame: the new glyphs, among them
  // one seen for the first time, are drawn already.
  REQUIRE(glyphQuads(game) == 7);
  const auto after = shot("shots/label_after.png");
  REQUIRE(compare(before, after, 32).fractionOver > 0.001); // the pixels changed
  sonnet::test::removeScratch(root);
}

TEST_CASE("the HUD stress scene builds two hundred elements of which a third are labels",
          "[runtime][samples][gui][gpu]") {
  Fixture fixture;
  const std::filesystem::path project = sampleProject(fixture.platform);
  if (project.empty()) {
    SKIP("the basic sample was not found in a checkout above the test binary");
  }
  const auto sink = std::make_shared<ProblemSink>();
  core::Log::addSink(sink);
  runtime::Game game{*fixture.window, *fixture.device, *fixture.swapchain, fixture.desc()};
  REQUIRE(game.open(project).has_value());
  REQUIRE(game.openScene("scenes/hud_stress.scene.json").has_value());
  // The first frame starts the script, the second lays out what it built and loads the font, and
  // the third measures the M15 budget's load: every element drawn, every label's glyphs with it.
  for (int i = 0; i < 3; ++i) {
    fixture.frame(game);
  }
  const auto elements = game.gui().elements();
  REQUIRE(elements.size() == 201); // the canvas and its two hundred tiles
  const auto labels = std::ranges::count_if(elements, [&](const gui::GuiElement &element) {
    return flecs::entity{game.world().ecs(), element.entity}.has<gui::GuiText>();
  });
  REQUIRE(labels == 67);
  // The labels count frames, so their text changes and is drawn every frame.
  REQUIRE(glyphQuads(game) >= 67);
  REQUIRE(
      std::ranges::any_of(game.graph().statistics().passes, [](const auto &pass) { return pass.name == "overlay"; }));
  REQUIRE(sink->problems.empty());
  core::Log::removeSink(sink);
}

// Issue #490: the character sample's obstacle course, from the project and from its bundle. Rays
// down onto every step, the deck, both ramps and the ledge block, and sideways and up in the
// narrow passage and under the low ceiling, land on the surfaces the meshes draw; the crate comes
// to rest on the floor and the character (issue #496) stands at the start; the platform shuttles
// across the pit and rests at its far end; and the character, put over the pit, falls into the
// kill volume and comes back standing on the respawn marker.
TEST_CASE("the character sample's course collides as drawn, its platform moves and its pit respawns",
          "[runtime][samples][gpu]") {
  Fixture fixture;
  const std::filesystem::path project = characterProject(fixture.platform);
  if (project.empty()) {
    SKIP("the character sample was not found in a checkout above the test binary");
  }
  // Played from the project: the route test below runs the course from the cooked bundle too.
  {
    const auto problems = std::make_shared<ProblemSink>();
    const auto messages = std::make_shared<MessageSink>();
    core::Log::addSink(problems);
    core::Log::addSink(messages);
    // The camera asks for the pointer lock, which the offscreen driver would refuse with a warning.
    test::RelativeModeWindow window{*fixture.window};
    runtime::Game game{window, *fixture.device, *fixture.swapchain, fixture.desc()};
    REQUIRE(game.open(project).has_value());
    world::World &world = game.world();
    const auto root = [&](std::string_view name) {
      for (const flecs::entity entity : world.roots()) {
        const world::Name *named = entity.try_get<world::Name>();
        if (named != nullptr && named->value == name) {
          return entity;
        }
      }
      return flecs::entity{};
    };
    const flecs::entity course = root("Course");
    const flecs::entity body = root("Character");
    REQUIRE(course.is_valid());
    REQUIRE(body.is_valid());
    const flecs::entity platform = world.findByPath(course, "Platform");
    const flecs::entity crate = world.findByPath(course, "Crate");
    REQUIRE(platform.is_valid());
    REQUIRE(crate.is_valid());
    const auto position = [](flecs::entity entity) { return entity.get<world::Transform>().position; };
    const float platformStart = position(platform).z;

    // One second: the bodies settle and the platform is still resting at its start.
    // The simulation alone, as the other gameplay tests step it: Lavapipe draws a frame slowly.
    const auto advance = [&](int frames) {
      for (int i = 0; i < frames; ++i) {
        game.update(1.0f / 60.0f);
      }
    };
    advance(60);
    REQUIRE(position(body).y == Approx(0.0f).margin(0.01f));
    REQUIRE(position(body).z == Approx(10.0f).margin(0.01f));
    REQUIRE(position(crate).y == Approx(0.45f).margin(0.04f));
    REQUIRE(position(platform).z == Approx(platformStart));

    physics::IPhysicsWorld &physics = game.physics();
    const auto down = [&](float x, float z) {
      const auto hit = physics.raycast({x, 10.0f, z}, {0.0f, -1.0f, 0.0f}, 20.0f);
      REQUIRE(hit.has_value());
      return *hit;
    };
    for (int step = 0; step < 4; ++step) {
      // Treads 0.3 m deep and 0.2 m high, the first one's front at z = 8.
      REQUIRE(down(-11.0f, 7.85f - 0.3f * static_cast<float>(step)).point.y ==
              Approx(0.2f * static_cast<float>(step + 1)).margin(0.001f));
    }
    for (int step = 0; step < 3; ++step) {
      REQUIRE(down(-7.7f + 0.6f * static_cast<float>(step), -1.0f).point.y ==
              Approx(0.5f * static_cast<float>(step + 1)).margin(0.001f));
    }
    REQUIRE(down(-3.0f, -1.0f).point.y == Approx(1.5f).margin(0.001f));
    REQUIRE(down(-2.0f, -16.0f).point.y == Approx(1.2f).margin(0.001f));
    // Halfway up each ramp the slope is 0.75 m high, and its normal leans by the ramp's angle.
    const float run20 = 1.5f / std::tan(glm::radians(20.0f));
    const physics::RaycastHit ramp20 = down(-1.5f, 1.0f + run20 / 2.0f);
    REQUIRE(ramp20.point.y == Approx(0.75f).margin(0.01f));
    REQUIRE(ramp20.normal.y == Approx(std::cos(glm::radians(20.0f))).margin(0.001f));
    const float run55 = 1.5f / std::tan(glm::radians(55.0f));
    const physics::RaycastHit ramp55 = down(2.0f + run55 / 2.0f, -1.0f);
    REQUIRE(ramp55.point.y == Approx(0.75f).margin(0.01f));
    REQUIRE(ramp55.normal.y == Approx(std::cos(glm::radians(55.0f))).margin(0.001f));
    REQUIRE(ramp55.normal.x > 0.8f);
    const auto ceiling = physics.raycast({10.5f, 1.0f, 4.0f}, {0.0f, 1.0f, 0.0f}, 5.0f);
    REQUIRE(ceiling.has_value());
    REQUIRE(ceiling->point.y == Approx(2.0f).margin(0.001f));
    const auto east = physics.raycast({8.5f, 1.0f, 4.0f}, {1.0f, 0.0f, 0.0f}, 5.0f);
    const auto west = physics.raycast({8.5f, 1.0f, 4.0f}, {-1.0f, 0.0f, 0.0f}, 5.0f);
    REQUIRE(east.has_value());
    REQUIRE(west.has_value());
    REQUIRE(east->point.x - west->point.x == Approx(0.8f).margin(0.001f));

    // Four more seconds: the platform has crossed and rests at the far end of its travel.
    advance(240);
    REQUIRE(position(platform).z == Approx(platformStart - 3.1f).margin(0.001f));

    // Over the pit beside the platform: the character falls into the kill volume and is put back.
    // Writing its Transform teleports it (docs/physics.md, "Characters").
    world::Transform drop = body.get<world::Transform>();
    drop.position = {5.0f, 0.0f, -6.0f};
    body.set(drop);
    advance(240);
    // One drawn frame: the course renders without a warning.
    fixture.frame(game);
    const bool respawned = std::ranges::any_of(
        messages->messages, [](const std::string &line) { return line == "Character fell and was respawned"; });
    const glm::vec3 back = position(body);
    core::Log::removeSink(messages);
    core::Log::removeSink(problems);
    REQUIRE(respawned);
    REQUIRE(back.x == Approx(0.0f).margin(0.05f));
    REQUIRE(back.z == Approx(10.0f).margin(0.05f));
    REQUIRE(back.y == Approx(0.0f).margin(0.01f));
    REQUIRE(problems->problems.empty());
  }
}

namespace {

// Every record from debug up, with the frame it came in, for the footsteps the character sample's
// script logs at debug.
class FrameSink final : public spdlog::sinks::base_sink<std::mutex> {
public:
  int frame{0};
  std::vector<std::pair<int, std::string>> messages;

protected:
  void sink_it_(const spdlog::details::log_msg &message) override {
    messages.emplace_back(frame, std::string(message.payload.data(), message.payload.size()));
  }
  void flush_() override {
  }
};

} // namespace

// Issue #496: the character sample's start scene played from the keyboard. W runs north, the way
// the camera looks, reaching the run speed; W and D together are no faster; letting go stops the
// character; Shift walks; the model turns to face the way it goes while the root keeps its yaw;
// Space jumps from the ground, a press just before landing is kept and jumps again on landing,
// and a press in mid-air long before landing is dropped. The camera asks for the pointer.
TEST_CASE("the character sample runs, walks, stops, turns and jumps from the keyboard", "[runtime][samples][gpu]") {
  Fixture fixture;
  const std::filesystem::path project = characterProject(fixture.platform);
  if (project.empty()) {
    SKIP("the character sample was not found in a checkout above the test binary");
  }
  const auto problems = std::make_shared<ProblemSink>();
  core::Log::addSink(problems);
  // The camera asks for the pointer lock, which the offscreen driver would refuse with a warning.
  test::RelativeModeWindow window{*fixture.window};
  runtime::Game game{window, *fixture.device, *fixture.swapchain, fixture.desc()};
  REQUIRE(game.open(project).has_value());
  world::World &world = game.world();
  flecs::entity character;
  for (const flecs::entity entity : world.roots()) {
    const world::Name *named = entity.try_get<world::Name>();
    if (named != nullptr && named->value == "Character") {
      character = entity;
    }
  }
  REQUIRE(character.is_valid());
  const flecs::entity model = world.findByPath(character, "Model");
  REQUIRE(model.is_valid());
  const auto frames = [&](int count) {
    for (int i = 0; i < count; ++i) {
      game.update(1.0f / 60.0f);
    }
  };
  const auto press = [&](platform::Key key) {
    game.event(platform::KeyPressed{.key = key, .modifiers = {}, .repeat = false});
  };
  const auto release = [&](platform::Key key) { game.event(platform::KeyReleased{.key = key, .modifiers = {}}); };
  const auto speed = [&] {
    const auto state = game.physics().character(character);
    REQUIRE(state.has_value());
    return glm::length(glm::vec2{state->velocity.x, state->velocity.z});
  };
  const auto position = [&] { return character.get<world::Transform>().position; };
  // The way the model faces, on the ground plane.
  const auto facing = [&] {
    const glm::vec3 forward = model.get<world::Transform>().rotation * glm::vec3{0.0f, 0.0f, -1.0f};
    return glm::normalize(glm::vec2{forward.x, forward.z});
  };

  frames(30);
  REQUIRE(game.scripts().mouseLockRequested());
  REQUIRE(game.input().mouseLocked());
  REQUIRE(game.physics().character(character)->grounded);
  const glm::vec3 start = position();

  // Running north, then north-east, then stopping.
  press(platform::Key::W);
  frames(45);
  CHECK(speed() == Approx(4.8f).margin(0.05f));
  CHECK(position().z < start.z - 2.5f);
  CHECK(std::abs(position().x - start.x) < 0.05f);
  CHECK(facing().y < -0.99f);
  press(platform::Key::D);
  frames(20);
  CHECK(speed() == Approx(4.8f).margin(0.05f));
  CHECK(facing().x > 0.6f);
  CHECK(facing().y < -0.6f);
  release(platform::Key::W);
  release(platform::Key::D);
  frames(30);
  CHECK(speed() < 0.01f);
  CHECK(character.get<world::Transform>().rotation.w == Approx(1.0f)); // the root never turns

  // Walking south with Shift. From standing facing north-east, that is a half turn on the spot,
  // which holds the walk back for up to `turnHold`, 0.6 s (issue #528).
  press(platform::Key::LeftShift);
  press(platform::Key::S);
  frames(75);
  CHECK(speed() == Approx(1.5f).margin(0.05f));
  CHECK(facing().y > 0.99f);
  release(platform::Key::S);
  release(platform::Key::LeftShift);
  frames(30);

  // A jump from standing, then a press 0.1 s before it lands, which jumps again on landing.
  const float ground = position().y;
  press(platform::Key::Space);
  frames(1);
  release(platform::Key::Space);
  float highest = ground;
  int airborne = 0;
  for (int i = 0; i < 120 && !(airborne > 0 && game.physics().character(character)->grounded); ++i) {
    frames(1);
    highest = std::max(highest, position().y);
    const auto state = game.physics().character(character);
    if (!state->grounded) {
      ++airborne;
    }
    // About 0.1 s before it comes down: falling, a quarter of a metre up.
    if (state->velocity.y < 0.0f && position().y < ground + 0.25f && position().y > ground + 0.05f) {
      press(platform::Key::Space);
      frames(1);
      release(platform::Key::Space);
      break;
    }
  }
  CHECK(highest == Approx(ground + 1.27f).margin(0.1f));
  float second = ground;
  for (int i = 0; i < 90; ++i) {
    frames(1);
    second = std::max(second, position().y);
  }
  CHECK(second > ground + 1.0f); // the buffered press jumped again

  // A press long before landing is forgotten: one jump only.
  frames(60);
  press(platform::Key::Space);
  frames(1);
  release(platform::Key::Space);
  frames(15); // rising
  press(platform::Key::Space);
  frames(1);
  release(platform::Key::Space);
  frames(90);
  float third = ground;
  for (int i = 0; i < 60; ++i) {
    frames(1);
    third = std::max(third, position().y);
  }
  CHECK(third < ground + 0.01f);
  core::Log::removeSink(problems);
  CHECK(problems->problems.empty());
}

// Issue #530: what character.lua does around the ground that the keyboard test does not reach.
// - Coyote time: walking off the ledge block, a jump pressed just after the feet leave it jumps;
//   one pressed after the coyote time does not, and is dropped before the character lands.
// - Landings: a hop picks no landing clip, a 1 m drop the soft one scaled below its full weight,
//   a 4.5 m drop the hard one; moving off a hard landing runs at once, and the landing fades
//   faster than standing through it.
// - Interrupted landing: a jump pressed during a hard landing jumps, the landing fades out, and
//   its clip is started once, not again each frame.
TEST_CASE("the character sample's coyote time and its hard and interrupted landings", "[runtime][samples][gpu]") {
  Fixture fixture;
  const std::filesystem::path project = characterProject(fixture.platform);
  if (project.empty()) {
    SKIP("the character sample was not found in a checkout above the test binary");
  }
  const auto problems = std::make_shared<ProblemSink>();
  core::Log::addSink(problems);
  // The camera asks for the pointer lock, which the offscreen driver would refuse with a warning.
  test::RelativeModeWindow window{*fixture.window};
  runtime::Game game{window, *fixture.device, *fixture.swapchain, fixture.desc()};
  REQUIRE(game.open(project).has_value());
  world::World &world = game.world();
  flecs::entity character;
  for (const flecs::entity entity : world.roots()) {
    const world::Name *named = entity.try_get<world::Name>();
    if (named != nullptr && named->value == "Character") {
      character = entity;
    }
  }
  REQUIRE(character.is_valid());
  const flecs::entity model = world.findByPath(character, "Model");
  REQUIRE(model.is_valid());
  const auto frames = [&](int count) {
    for (int i = 0; i < count; ++i) {
      game.update(1.0f / 60.0f);
    }
  };
  const auto press = [&](platform::Key key) {
    game.event(platform::KeyPressed{.key = key, .modifiers = {}, .repeat = false});
  };
  const auto release = [&](platform::Key key) { game.event(platform::KeyReleased{.key = key, .modifiers = {}}); };
  const auto tap = [&](platform::Key key) {
    press(key);
    frames(1);
    release(key);
  };
  const auto grounded = [&] {
    const auto state = game.physics().character(character);
    REQUIRE(state.has_value());
    return state->grounded;
  };
  const auto speed = [&] {
    const auto state = game.physics().character(character);
    REQUIRE(state.has_value());
    return glm::length(glm::vec2{state->velocity.x, state->velocity.z});
  };
  const auto position = [&] { return character.get<world::Transform>().position; };
  // Writing the Transform teleports the character (docs/physics.md, "Characters").
  const auto teleport = [&](glm::vec3 to) {
    world::Transform transform = character.get<world::Transform>();
    transform.position = to;
    character.set(transform);
  };
  // The script's layers: Idle, Run, JumpStart, Fall, Land, HardLand, Turn.
  constexpr std::size_t jumpLayer = 2;
  constexpr std::size_t fallLayer = 3;
  constexpr std::size_t landLayer = 4;
  constexpr std::size_t hardLayer = 5;
  const auto layer = [&](std::size_t index) {
    const world::Animator &animator = model.get<world::Animator>();
    REQUIRE(animator.layers.size() == 7);
    return animator.layers[index];
  };
  // Steps until the character is on the ground after having left it; false after five seconds.
  const auto untilLanded = [&] {
    bool left = false;
    for (int i = 0; i < 300; ++i) {
      frames(1);
      left = left || !grounded();
      if (left && grounded()) {
        return true;
      }
    }
    return false;
  };

  // The humanoid's clips are imported on the asset workers the first time an Animator asks: a
  // landing clip's time moves only once they are in.
  {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(2);
    while (model.get<world::Animator>().time == 0.0f && std::chrono::steady_clock::now() < deadline) {
      frames(1);
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(model.get<world::Animator>().time > 0.0f);
  }
  // The floor south of the ledge block, where its walks end.
  {
    const auto floor = game.physics().raycast({-2.0f, 5.0f, -12.5f}, {0.0f, -1.0f, 0.0f}, 10.0f);
    REQUIRE(floor.has_value());
    REQUIRE(floor->point.y == Approx(0.0f).margin(0.001f));
  }

  // Walks south off the 1.2 m ledge block and presses Space `late` frames after the feet leave
  // it. Returns how high above the block the character then rises.
  constexpr float ledge = 1.2f;
  const auto walkOffAndJump = [&](int late) {
    teleport({-2.0f, ledge, -16.0f});
    frames(30);
    REQUIRE(grounded());
    REQUIRE(position().y == Approx(ledge).margin(0.01f));
    press(platform::Key::LeftShift);
    press(platform::Key::S);
    int step = 0;
    while (grounded() && step < 240) {
      frames(1);
      ++step;
    }
    REQUIRE_FALSE(grounded());
    REQUIRE(position().z > -14.0f); // over the edge, not a step down
    frames(late);
    tap(platform::Key::Space);
    float highest = position().y;
    for (int i = 0; i < 90; ++i) {
      frames(1);
      highest = std::max(highest, position().y);
    }
    release(platform::Key::S);
    release(platform::Key::LeftShift);
    // Down on the floor and staying there: the press was not kept for the landing.
    float after = position().y;
    for (int i = 0; i < 30; ++i) {
      frames(1);
      after = std::max(after, position().y);
    }
    CHECK(after < 0.01f);
    return highest - ledge;
  };
  // 2 frames is 0.03 s, inside the 0.12 s coyote time; 12 frames, 0.2 s, is outside it, and its
  // 0.15 s jump buffer runs out before the 0.5 s fall ends.
  CHECK(walkOffAndJump(2) > 0.8f);
  CHECK(walkOffAndJump(12) < 0.01f);

  // Drops from `height` above the floor at the start and returns the highest weight each landing
  // layer reaches in the second after touching down.
  const glm::vec3 start{0.0f, 0.0f, 10.0f};
  const auto drop = [&](float height) {
    teleport(start);
    frames(5); // the last drop's landing faded in its second
    teleport(start + glm::vec3{0.0f, height, 0.0f});
    REQUIRE(untilLanded());
    std::pair<float, float> peak{0.0f, 0.0f};
    for (int i = 0; i < 60; ++i) {
      peak.first = std::max(peak.first, layer(landLayer).weight);
      peak.second = std::max(peak.second, layer(hardLayer).weight);
      frames(1);
    }
    return peak;
  };
  {
    // 0.25 m: 2.2 m/s down, under the 2.5 m/s a landing needs.
    const auto [land, hard] = drop(0.25f);
    CHECK(land < 0.01f);
    CHECK(hard < 0.01f);
  }
  {
    // 1 m: 4.4 m/s, the soft landing at (4.4 - 2.5) / 2.5 of its weight.
    const auto [land, hard] = drop(1.0f);
    CHECK(land == Approx(0.77f).margin(0.1f));
    CHECK(hard < 0.01f);
  }
  {
    // 4.5 m: 9.4 m/s, over `hardLanding`'s 7 m/s.
    const auto [land, hard] = drop(4.5f);
    CHECK(land < 0.01f);
    CHECK(hard > 0.95f);
  }

  // Half a second into a hard landing, standing through it and running off it.
  const auto hardLanding = [&](bool run) {
    teleport(start);
    frames(5);
    teleport(start + glm::vec3{0.0f, 4.5f, 0.0f});
    REQUIRE(untilLanded());
    if (run) {
      press(platform::Key::W);
    }
    frames(30);
    const std::pair<float, float> result{layer(hardLayer).weight, speed()};
    if (run) {
      release(platform::Key::W);
    }
    return result;
  };
  const auto [standingWeight, standingSpeed] = hardLanding(false);
  const auto [runningWeight, runningSpeed] = hardLanding(true);
  CHECK(standingSpeed < 0.01f);
  CHECK(standingWeight > 0.3f);
  CHECK(runningSpeed > 3.0f); // a landing does not hold the character
  CHECK(runningWeight < 0.8f * standingWeight);

  // A jump pressed during a hard landing.
  teleport(start);
  frames(5);
  teleport(start + glm::vec3{0.0f, 4.5f, 0.0f});
  REQUIRE(untilLanded());
  frames(6);
  const float landed = position().y;
  float hardTime = layer(hardLayer).time;
  CHECK(hardTime > 0.05f);
  CHECK(layer(hardLayer).weight > 0.9f);
  tap(platform::Key::Space);
  float highest = landed;
  float jumpWeight = 0.0f;
  float airWeight = 0.0f;
  bool reset = false;
  int faded = -1;
  // Up and down again: the hard landing's clip only moves on, and its weight goes within 0.2 s.
  for (int i = 0; i < 90; ++i) {
    frames(1);
    highest = std::max(highest, position().y);
    jumpWeight = std::max(jumpWeight, layer(jumpLayer).weight);
    airWeight = std::max(airWeight, layer(jumpLayer).weight + layer(fallLayer).weight);
    const world::AnimationLayer hard = layer(hardLayer);
    reset = reset || hard.time < hardTime;
    hardTime = hard.time;
    if (faded < 0 && hard.weight < 0.01f) {
      faded = i;
    }
  }
  CHECK(highest > landed + 1.0f);
  // JumpStart plays, giving way to Fall on the way up, and the air layers take over.
  CHECK(jumpWeight > 0.3f);
  CHECK(airWeight > 0.95f);
  CHECK_FALSE(reset);
  CHECK(faded >= 0);
  CHECK(faded <= 12);

  core::Log::removeSink(problems);
  CHECK(problems->problems.empty());
}

// Issue #528: turning on the spot. From standing, a tap of a key a quarter turn and then a half turn
// off the way the model faces plays a turn clip of that size and side: the model ends facing the
// key's direction, the capsule stays where it stood, and the toes, while planted, move under 0.7 of
// what they would if the idle were swung round on them (the arc they sweep at their distance from
// the capsule's axis, as the script did before; 0.5 to 0.6 measured, the rest being the capture's
// own shuffle and pivots on the ball of the foot). An input less than `turnThreshold` off still
// turns the model smoothly as the character moves off, with no turn clip.
TEST_CASE("the character sample turns on the spot with a turn clip, its planted feet staying put",
          "[runtime][samples][gpu]") {
  Fixture fixture;
  const std::filesystem::path project = characterProject(fixture.platform);
  if (project.empty()) {
    SKIP("the character sample was not found in a checkout above the test binary");
  }
  const auto problems = std::make_shared<ProblemSink>();
  core::Log::addSink(problems);
  // The camera asks for the pointer lock, which the offscreen driver would refuse with a warning.
  test::RelativeModeWindow window{*fixture.window};
  runtime::Game game{window, *fixture.device, *fixture.swapchain, fixture.desc()};
  REQUIRE(game.open(project).has_value());
  world::World &world = game.world();
  flecs::entity character;
  for (const flecs::entity entity : world.roots()) {
    const world::Name *named = entity.try_get<world::Name>();
    if (named != nullptr && named->value == "Character") {
      character = entity;
    }
  }
  REQUIRE(character.is_valid());
  const flecs::entity model = world.findByPath(character, "Model");
  REQUIRE(model.is_valid());
  const auto toes = [&](std::string_view side) {
    const std::string leg = std::format("pelvis/thigh_{0}/calf_{0}/foot_{0}/ball_{0}", side);
    flecs::entity foot = world.findByPath(model, "Armature/Root/" + leg);
    if (!foot.is_valid()) {
      foot = world.findByPath(model, "Root/" + leg);
    }
    return foot;
  };
  const std::array feet{toes("l"), toes("r")};
  REQUIRE(feet[0].is_valid());
  REQUIRE(feet[1].is_valid());
  const auto frames = [&](int count) {
    for (int i = 0; i < count; ++i) {
      game.update(1.0f / 60.0f);
    }
  };
  const auto press = [&](platform::Key key) {
    game.event(platform::KeyPressed{.key = key, .modifiers = {}, .repeat = false});
  };
  const auto release = [&](platform::Key key) { game.event(platform::KeyReleased{.key = key, .modifiers = {}}); };
  const auto position = [&] { return character.get<world::Transform>().position; };
  // The model's yaw about +Y, 0 facing -Z, as character.lua measures it.
  const auto facing = [&] {
    const glm::vec3 forward = model.get<world::Transform>().rotation * glm::vec3{0.0f, 0.0f, -1.0f};
    return std::atan2(-forward.x, -forward.z);
  };
  const auto wrap = [](float angle) { return std::remainder(angle, 2.0f * glm::pi<float>()); };
  // The turn layer, the script's seventh: Idle, Run, JumpStart, Fall, Land, HardLand, Turn.
  const auto turnLayer = [&] {
    const world::Animator &animator = model.get<world::Animator>();
    REQUIRE(animator.layers.size() == 7);
    return animator.layers[6];
  };
  const auto clip = [](std::string_view text) {
    const std::optional<core::Uuid> id = core::Uuid::parse(text);
    REQUIRE(id.has_value());
    return *id;
  };
  const core::Uuid turnRight90 = clip("628e9da2-02b1-8a6b-af6f-c70e4c1d97f8");
  const core::Uuid turnLeft180 = clip("3eb8cf51-7a29-84fd-b531-fa1c3dd75130");
  const core::Uuid turnRight180 = clip("f1b9cf51-7a2a-84fd-822f-fa1c3dd65130");

  // The humanoid's clips are imported on the asset workers the first time an Animator asks: play
  // until its time moves, then a second more, standing at the start facing -Z.
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(2);
  while (model.get<world::Animator>().time == 0.0f && std::chrono::steady_clock::now() < deadline) {
    game.update(1.0f / 60.0f);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  REQUIRE(model.get<world::Animator>().time > 0.0f);
  frames(60);
  REQUIRE(game.physics().character(character)->grounded);
  CHECK(std::abs(facing()) < 0.01f);

  struct Turn {
    core::Uuid clip;
    float peak{0.0f};               // the turn layer's highest weight
    float played{0.0f};             // how far its clip got, in seconds
    float turned{0.0f};             // the model's yaw at the end less at the start
    float moved{0.0f};              // the farthest the capsule got from where it stood
    std::array<float, 2> planted{}; // how far each foot's toes travelled while planted
    std::array<float, 2> swept{};   // the arc they would sweep on the idle, swung by `turned`
  };
  // Taps a key (one frame down) and plays until the turn layer has faded and the model is still.
  const auto turn = [&](platform::Key key) {
    Turn result;
    const glm::vec3 start = position();
    const float from = facing();
    std::array<std::vector<glm::vec3>, 2> path;
    for (std::size_t foot = 0; foot < 2; ++foot) {
      const glm::vec3 at{feet[foot].get<world::WorldTransform>().matrix[3]};
      path[foot].push_back(at);
    }
    press(key);
    frames(1);
    release(key);
    float last = facing();
    int still = 0;
    for (int frame = 0; frame < 60 * 5 && still < 10; ++frame) {
      frames(1);
      const world::AnimationLayer layer = turnLayer();
      if (layer.weight > result.peak) {
        result.peak = layer.weight;
        result.clip = layer.clip;
      }
      result.played = std::max(result.played, layer.time);
      result.moved = std::max(result.moved, glm::length(position() - start));
      for (std::size_t foot = 0; foot < 2; ++foot) {
        path[foot].emplace_back(feet[foot].get<world::WorldTransform>().matrix[3]);
      }
      still = (result.peak > 0.0f && layer.weight == 0.0f && std::abs(wrap(facing() - last)) < 1e-5f) ? still + 1 : 0;
      last = facing();
    }
    result.turned = wrap(facing() - from);
    for (std::size_t foot = 0; foot < 2; ++foot) {
      float lowest = path[foot].front().y;
      for (const glm::vec3 &p : path[foot]) {
        lowest = std::min(lowest, p.y);
      }
      // Planted as the clips' contacts are measured (LICENSES.md): within 2.5 cm of the lowest and
      // slower than 0.6 m/s. A step the turn takes is neither; on the idle swung round, the toes
      // never leave the floor, so all of the arc is sliding.
      for (std::size_t i = 1; i < path[foot].size(); ++i) {
        const glm::vec3 a = path[foot][i - 1];
        const glm::vec3 b = path[foot][i];
        const float step = glm::length(glm::vec2{b.x - a.x, b.z - a.z});
        if (a.y < lowest + 0.025f && b.y < lowest + 0.025f && step * 60.0f < 0.6f) {
          result.planted[foot] += step;
        }
      }
      const glm::vec3 first = path[foot].front();
      result.swept[foot] = glm::length(glm::vec2{first.x - start.x, first.z - start.z}) * std::abs(result.turned);
    }
    return result;
  };

  // A quarter turn to the right: D, with the camera behind the character looking along -Z.
  const Turn quarter = turn(platform::Key::D);
  INFO("quarter turn: turned " << glm::degrees(quarter.turned) << " degrees, planted " << quarter.planted[0] << " and "
                               << quarter.planted[1] << " m, swept " << quarter.swept[0] << " and " << quarter.swept[1]
                               << " m, moved " << quarter.moved << " m");
  CHECK(quarter.clip == turnRight90);
  CHECK(quarter.peak > 0.95f);
  CHECK(quarter.played == Approx(42.0f / 30.0f).margin(0.01f)); // to the end of its clip
  CHECK(glm::degrees(wrap(facing() + glm::half_pi<float>())) == Approx(0.0f).margin(1.0f));
  CHECK(quarter.moved < 0.01f);
  for (std::size_t foot = 0; foot < 2; ++foot) {
    CHECK(quarter.swept[foot] > 0.1f);
    CHECK(quarter.planted[foot] < 0.7f * quarter.swept[foot]);
  }

  // A half turn, to face +X's opposite: A, either way round.
  const Turn half = turn(platform::Key::A);
  INFO("half turn: turned " << glm::degrees(half.turned) << " degrees, planted " << half.planted[0] << " and "
                            << half.planted[1] << " m, swept " << half.swept[0] << " and " << half.swept[1]
                            << " m, moved " << half.moved << " m");
  CHECK((half.clip == turnLeft180 || half.clip == turnRight180));
  CHECK(half.peak > 0.95f);
  CHECK(glm::degrees(wrap(facing() - glm::half_pi<float>())) == Approx(0.0f).margin(1.0f));
  CHECK(half.moved < 0.01f);
  for (std::size_t foot = 0; foot < 2; ++foot) {
    CHECK(half.swept[foot] > 0.2f);
    CHECK(half.planted[foot] < 0.7f * half.swept[foot]);
  }

  // 45 degrees off, below the threshold: no turn clip, the model turns as the character runs off.
  const glm::vec3 stood = position();
  press(platform::Key::A);
  press(platform::Key::W);
  float weight = 0.0f;
  for (int i = 0; i < 30; ++i) {
    frames(1);
    weight = std::max(weight, turnLayer().weight);
  }
  release(platform::Key::A);
  release(platform::Key::W);
  CHECK(weight == 0.0f);
  CHECK(glm::degrees(wrap(facing() - glm::quarter_pi<float>())) == Approx(0.0f).margin(2.0f));
  CHECK(glm::length(position() - stood) > 1.0f);

  core::Log::removeSink(problems);
  CHECK(problems->problems.empty());
}

// Issue #496: the character sample's route scene runs the course by itself through the same
// controller the keys drive: through the passage and the tunnel, up the ramp and down the steps,
// over the pit on the platform and onto the ledge block with a jump, to the last waypoint, without
// a respawn or a warning. On the way:
// - a planted foot stays where it was put while the character runs and walks: the playback rate
//   follows the ground speed over the clips' strides;
// - footsteps alternate feet and no foot sounds twice for one step, though Walk and Run both cross
//   it when they are blended;
// - the camera is never inside the course: a sphere around its near plane overlaps nothing.
// Then a respawn and opening the scene again each start the route over from the start.
TEST_CASE("the character sample's route runs the course with planted feet, single footsteps and a clear camera",
          "[runtime][samples][gpu]") {
  Fixture fixture;
  const std::filesystem::path project = characterProject(fixture.platform);
  if (project.empty()) {
    SKIP("the character sample was not found in a checkout above the test binary");
  }
  const auto problems = std::make_shared<ProblemSink>();
  const auto messages = std::make_shared<FrameSink>();
  core::Log::addSink(problems);
  core::Log::addSink(messages);
  // The camera asks for the pointer lock, which the offscreen driver would refuse with a warning.
  test::RelativeModeWindow window{*fixture.window};
  runtime::Game game{window, *fixture.device, *fixture.swapchain, fixture.desc()};
  REQUIRE(game.open(project).has_value());
  REQUIRE(game.openScene("scenes/route.scene.json").has_value());
  world::World &world = game.world();
  physics::IPhysicsWorld &physics = game.physics();
  const auto root = [&](std::string_view name) {
    for (const flecs::entity entity : world.roots()) {
      const world::Name *named = entity.try_get<world::Name>();
      if (named != nullptr && named->value == name) {
        return entity;
      }
    }
    return flecs::entity{};
  };
  const auto said = [&](std::string_view text) {
    return std::ranges::count_if(messages->messages, [&](const auto &record) { return record.second == text; });
  };
  const auto step = [&] {
    game.update(1.0f / 60.0f);
    ++messages->frame;
  };
  // The humanoid's clips are imported on the asset workers the first time an Animator asks: play
  // until its time moves, then open the scene again so the route starts with them loaded.
  {
    const flecs::entity model = world.findByPath(root("Character"), "Model");
    REQUIRE(model.is_valid());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(2);
    while (model.get<world::Animator>().time == 0.0f && std::chrono::steady_clock::now() < deadline) {
      game.update(1.0f / 60.0f);
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(model.get<world::Animator>().time > 0.0f);
  }
  REQUIRE(game.openScene("scenes/route.scene.json").has_value());
  messages->messages.clear();

  // The course, with the route the character starts on and ends near.
  {
    const flecs::entity character = root("Character");
    const flecs::entity camera = root("Camera");
    REQUIRE(character.is_valid());
    REQUIRE(camera.is_valid());
    const flecs::entity model = world.findByPath(character, "Model");
    REQUIRE(model.is_valid());
    flecs::entity foot = world.findByPath(model, "Armature/Root/pelvis/thigh_l/calf_l/foot_l/ball_l");
    if (!foot.is_valid()) {
      foot = world.findByPath(model, "Root/pelvis/thigh_l/calf_l/foot_l/ball_l");
    }
    REQUIRE(foot.is_valid());
    const auto position = [](flecs::entity entity) { return entity.get<world::Transform>().position; };

    // The toes' world positions while the character runs its first, straight leg (at 4.8 m/s) and
    // walks the passage (at 1.5 m/s): a foot is planted while it is within 2 cm of its lowest.
    struct Sample {
      int frame;
      glm::vec3 toes;
    };
    std::vector<Sample> run;
    std::vector<Sample> walk;
    const std::array ignore{character};
    int cameraOverlaps = 0;
    int frames = 0;
    // The frame the character sets off: the first leg starts with a quarter turn on the spot, which
    // holds it back for a while (issue #528). The windows count from there.
    int off = -1;
    for (; frames < 60 * 70 && (frames % 30 != 0 || said("Character finished its route") == 0); ++frames) {
      step();
      const glm::vec3 toes{foot.get<world::WorldTransform>().matrix[3]};
      if (off < 0 && glm::length(glm::vec2{physics.character(character)->velocity.x,
                                           physics.character(character)->velocity.z}) > 0.2f) {
        off = frames;
      }
      if (off >= 0 && frames >= off + 36 && frames < off + 84) {
        run.push_back({frames, toes});
      } else if (off >= 0 && frames >= off + 200 && frames < off + 320) {
        walk.push_back({frames, toes});
      }
      if (physics.sphereCast(position(camera), 0.14f, glm::vec3{0.0f}, 0.0f, ignore).has_value()) {
        ++cameraOverlaps;
      }
    }
    // How fast a planted foot moves over the ground: the median over the frames it is down, since
    // the band also takes in the frames it comes down and lifts off in.
    const auto planted = [](const std::vector<Sample> &samples) {
      float lowest = samples.front().toes.y;
      for (const Sample &sample : samples) {
        lowest = std::min(lowest, sample.toes.y);
      }
      std::vector<float> speeds;
      for (std::size_t i = 1; i < samples.size(); ++i) {
        const Sample &a = samples[i - 1];
        const Sample &b = samples[i];
        if (b.frame == a.frame + 1 && a.toes.y < lowest + 0.02f && b.toes.y < lowest + 0.02f) {
          speeds.push_back(glm::length(glm::vec2{b.toes.x - a.toes.x, b.toes.z - a.toes.z}) * 60.0f);
        }
      }
      REQUIRE(speeds.size() >= 4);
      std::ranges::sort(speeds);
      return speeds[speeds.size() / 2];
    };
    const float runSlide = planted(run);
    const float walkSlide = planted(walk);
    INFO("planted toes slide at " << runSlide << " m/s running and " << walkSlide << " m/s walking");
    CHECK(runSlide < 0.3f);
    CHECK(walkSlide < 0.15f);

    REQUIRE(said("Character finished its route") == 1);
    CHECK(said("Character fell and was respawned") == 0);
    CHECK(cameraOverlaps == 0);
    const glm::vec3 end = position(character);
    CHECK(end.x == Approx(3.0f).margin(0.6f));
    CHECK(end.z == Approx(-20.5f).margin(0.6f));
    CHECK(end.y == Approx(0.0f).margin(0.01f));

    // Footsteps: both feet, about one per 0.3 to 0.6 s over the run, and never the same foot twice
    // within a quarter of a second, which a step crossed by both blended clips, or a landing's own
    // step just after a walking one, would do.
    std::vector<std::pair<int, bool>> steps;
    for (const auto &[frame, text] : messages->messages) {
      if (text.starts_with("footstep left ") || text.starts_with("footstep right ")) {
        steps.emplace_back(frame, text.starts_with("footstep left "));
      }
    }
    INFO(steps.size() << " footsteps in " << frames << " frames");
    CHECK(steps.size() > 40);
    int doubled = 0;
    for (std::size_t i = 1; i < steps.size(); ++i) {
      for (std::size_t j = 0; j < i; ++j) {
        if (steps[i].first - steps[j].first < 15 && steps[j].second == steps[i].second) {
          UNSCOPED_INFO("doubled at frames " << steps[j].first << " and " << steps[i].first);
          ++doubled;
        }
      }
    }
    CHECK(doubled == 0);

    // Over the pit beside the platform: the kill volume puts the character back at the start, and
    // the route starts over towards its first waypoint, east and a little south.
    world::Transform drop = character.get<world::Transform>();
    drop.position = {5.0f, 0.0f, -6.0f};
    character.set(drop);
    for (int i = 0; i < 240 && said("Character fell and was respawned") == 0; ++i) {
      step();
    }
    REQUIRE(said("Character fell and was respawned") == 1);
    REQUIRE(position(character).z == Approx(10.0f).margin(0.05f));
    for (int i = 0; i < 70; ++i) { // the quarter turn on the spot first, then off
      step();
    }
    CHECK(position(character).x > 0.5f);
    CHECK(position(character).z > 10.0f);
  }

  // Opening the scene again, as stopping and starting play does, starts the route over too.
  REQUIRE(game.openScene("scenes/route.scene.json").has_value());
  {
    const flecs::entity character = root("Character");
    REQUIRE(character.is_valid());
    REQUIRE(character.get<world::Transform>().position.z == Approx(10.0f));
    for (int i = 0; i < 70; ++i) { // the quarter turn on the spot first, then off
      step();
    }
    const glm::vec3 moved = character.get<world::Transform>().position;
    CHECK(moved.x > 0.5f);
    CHECK(moved.z > 10.0f);
  }
  core::Log::removeSink(messages);
  core::Log::removeSink(problems);
  CHECK(problems->problems.empty());
}

namespace {

// What one run of the character sample's route did, for comparing a run from the project folder
// with one from the cooked bundle.
struct RouteTrace {
  int finishedAt{-1}; // the step that logged the route's end
  glm::vec3 end{};
  int footsteps{0};
  int respawns{0};             // before the drop into the pit
  bool rode{false};            // stood on the moving platform while it moved
  std::array<float, 7> peak{}; // the highest weight each of the Animator's layers reached
  glm::vec3 respawned{};       // where the kill volume put it back
  std::vector<std::string> problems;
};

} // namespace

// Issue #497: the route scene from the project folder and from the cooked bundle, as the player
// runs either. Each run moves, turns where the route turns, walks, rides the platform, jumps onto
// the ledge block, falls off its far side and lands, and is respawned after a drop into the pit;
// the two runs agree on where the character went, the clips it played and the steps it took. The
// bundle then runs a capture scene the way `sonnet_player <bundle> --scene ... --play` does.
TEST_CASE("the character sample's route plays the same from its folder and from a cooked bundle",
          "[runtime][samples][gpu]") {
  Fixture fixture;
  const std::filesystem::path project = characterProject(fixture.platform);
  if (project.empty()) {
    SKIP("the character sample was not found in a checkout above the test binary");
  }
  const std::filesystem::path out = std::filesystem::temp_directory_path() / "sonnet_runtime_character_bundle";
  std::filesystem::remove_all(out);
  {
    // Cooked through a Game, so the cook and the run use one database as they do in the editor.
    runtime::Game game{*fixture.window, *fixture.device, *fixture.swapchain, fixture.desc()};
    REQUIRE(game.open(project).has_value());
    const auto opened = assets::Project::open(project);
    REQUIRE(opened.has_value());
    const auto report = assets::cook(game.assets(), *opened, {.outputDirectory = out});
    REQUIRE(report.has_value());
    REQUIRE(report->warnings.empty());
  }

  const auto play = [&](const std::filesystem::path &source) {
    RouteTrace trace;
    const auto problems = std::make_shared<ProblemSink>();
    const auto messages = std::make_shared<FrameSink>();
    core::Log::addSink(problems);
    core::Log::addSink(messages);
    {
      // The camera asks for the pointer lock, which the offscreen driver would refuse with a warning.
      test::RelativeModeWindow window{*fixture.window};
      runtime::Game game{window, *fixture.device, *fixture.swapchain, fixture.desc()};
      REQUIRE(game.open(source).has_value());
      REQUIRE(game.openScene("scenes/route.scene.json").has_value());
      world::World &world = game.world();
      const auto character = [&] {
        for (const flecs::entity entity : world.roots()) {
          const world::Name *named = entity.try_get<world::Name>();
          if (named != nullptr && named->value == "Character") {
            return entity;
          }
        }
        return flecs::entity{};
      };
      const auto said = [&](std::string_view text) {
        return std::ranges::count_if(messages->messages, [&](const auto &record) { return record.second == text; });
      };
      // The clips load on the asset workers the first time the Animator asks, from the bundle as
      // from the project: play until they move, then open the scene again, as the route test does.
      {
        const flecs::entity model = world.findByPath(character(), "Model");
        REQUIRE(model.is_valid());
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(2);
        while (model.get<world::Animator>().time == 0.0f && std::chrono::steady_clock::now() < deadline) {
          game.update(1.0f / 60.0f);
          std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        REQUIRE(model.get<world::Animator>().time > 0.0f);
      }
      REQUIRE(game.openScene("scenes/route.scene.json").has_value());
      messages->messages.clear();
      messages->frame = 0;
      problems->problems.clear();

      const flecs::entity body = character();
      REQUIRE(body.is_valid());
      const flecs::entity model = world.findByPath(body, "Model");
      REQUIRE(model.is_valid());
      const auto step = [&] {
        game.update(1.0f / 60.0f);
        ++messages->frame;
      };
      for (int frame = 0; frame < 60 * 70 && said("Character finished its route") == 0; ++frame) {
        step();
        const auto state = game.physics().character(body);
        REQUIRE(state.has_value());
        if (state->grounded && glm::length(state->supportVelocity) > 0.5f) {
          trace.rode = true;
        }
        const std::vector<world::AnimationLayer> &layers = model.get<world::Animator>().layers;
        REQUIRE(layers.size() == trace.peak.size());
        for (std::size_t i = 0; i < layers.size(); ++i) {
          trace.peak[i] = std::max(trace.peak[i], layers[i].weight);
        }
      }
      for (const auto &[frame, text] : messages->messages) {
        if (text == "Character finished its route") {
          trace.finishedAt = frame;
        }
        if (text.starts_with("footstep left ") || text.starts_with("footstep right ")) {
          ++trace.footsteps;
        }
      }
      trace.end = body.get<world::Transform>().position;
      trace.respawns = static_cast<int>(said("Character fell and was respawned"));

      // Over the pit beside the platform: the kill volume puts the character back at the start.
      world::Transform drop = body.get<world::Transform>();
      drop.position = {5.0f, 0.0f, -6.0f};
      body.set(drop);
      for (int i = 0; i < 240 && said("Character fell and was respawned") == trace.respawns; ++i) {
        step();
      }
      trace.respawned = body.get<world::Transform>().position;
    }
    core::Log::removeSink(messages);
    core::Log::removeSink(problems);
    trace.problems = problems->problems;
    return trace;
  };

  const RouteTrace folder = play(project);
  const RouteTrace bundle = play(out / "game.sbundle");
  // The Animator's layers, in character.lua's order after Walk, its own clip.
  enum Layer : std::uint8_t {
    Idle,
    Run,
    JumpStart,
    Fall,
    Land,
    HardLand,
    Turn
  };
  for (const RouteTrace *trace : {&folder, &bundle}) {
    INFO((trace == &folder ? "from the folder" : "from the bundle"));
    CHECK(trace->problems.empty());
    REQUIRE(trace->finishedAt > 0);
    CHECK(trace->respawns == 0);
    CHECK(trace->end.x == Approx(3.0f).margin(0.6f));
    CHECK(trace->end.z == Approx(-20.5f).margin(0.6f));
    CHECK(trace->end.y == Approx(0.0f).margin(0.01f));
    CHECK(trace->rode);
    CHECK(trace->footsteps > 40);
    CHECK(trace->peak[Idle] == Approx(1.0f));
    CHECK(trace->peak[Run] > 0.9f);
    CHECK(trace->peak[JumpStart] > 0.4f); // the jump onto the ledge block, faded in
    CHECK(trace->peak[Fall] > 0.5f);      // off its far side, 1.2 m down
    CHECK(trace->peak[Land] > 0.3f);      // and the landing at the bottom
    CHECK(trace->peak[HardLand] == 0.0f); // no drop on the route is that hard
    CHECK(trace->peak[Turn] > 0.5f);      // the first leg starts with a quarter turn on the spot
    CHECK(trace->respawned.x == Approx(0.0f).margin(0.05f));
    CHECK(trace->respawned.z == Approx(10.0f).margin(0.05f));
  }
  // The same simulation from the same scene: Jolt is deterministic on one machine and build (no
  // promise across platforms), and the bundle's cooked colliders and clips are the project's.
  CHECK(bundle.finishedAt == folder.finishedAt);
  CHECK(glm::length(bundle.end - folder.end) < 0.001f);
  CHECK(bundle.footsteps == folder.footsteps);
  for (std::size_t i = 0; i < folder.peak.size(); ++i) {
    CHECK(bundle.peak[i] == Approx(folder.peak[i]).margin(0.001f));
  }

  // A capture scene from the bundle, through the player's own flags: it plays its route for the
  // documented time and writes the still.
  const std::filesystem::path shot = out / "landing.png"; // absolute, so it stays where it is
  {
    test::RelativeModeWindow window{*fixture.window};
    runtime::Game game{window, *fixture.device, *fixture.swapchain, fixture.desc()};
    REQUIRE(game.open(out / "game.sbundle").has_value());
    runtime::CaptureRun run{playerCapture({"--scene", "scenes/capture_landing.scene.json", "--play", "1.4",
                                           "--settle-frames", "1", "--screenshot", shot.string()})};
    REQUIRE(fixture.capture(game, run) == runtime::CaptureRun::Status::Done);
  }
  REQUIRE(std::filesystem::is_regular_file(shot));
  sonnet::test::removeScratch(out);
}

// Issue #497: the start scene from the keyboard at 30, 60 and 144 rendered frames a second. The
// world steps at a fixed 1/60 s whatever the frame rate: at 30 each frame runs two steps, at 144
// most frames run none and draw the character interpolated between steps. Two seconds of running
// east cover the same ground; a Space press in a frame without a step still jumps, as high; and
// the camera, placed after the interpolation, moves by the same distance every frame while the
// character runs at a constant speed, so the view does not judder.
TEST_CASE("the character sample runs and jumps the same at 30, 60 and 144 frames a second", "[runtime][samples][gpu]") {
  Fixture fixture;
  const std::filesystem::path project = characterProject(fixture.platform);
  if (project.empty()) {
    SKIP("the character sample was not found in a checkout above the test binary");
  }
  struct Result {
    float travel{0.0f}; // m east in two seconds from standing
    float height{0.0f}; // m, the top of a jump from standing
    float judder{0.0f}; // the largest difference of one frame's camera motion from the mean, over the mean
  };
  const auto play = [&](int rate) {
    Result result;
    const auto problems = std::make_shared<ProblemSink>();
    core::Log::addSink(problems);
    {
      // The camera asks for the pointer lock, which the offscreen driver would refuse with a warning.
      test::RelativeModeWindow window{*fixture.window};
      runtime::Game game{window, *fixture.device, *fixture.swapchain, fixture.desc()};
      REQUIRE(game.open(project).has_value());
      world::World &world = game.world();
      flecs::entity character;
      flecs::entity camera;
      for (const flecs::entity entity : world.roots()) {
        const world::Name *named = entity.try_get<world::Name>();
        if (named != nullptr && named->value == "Character") {
          character = entity;
        } else if (named != nullptr && named->value == "Camera") {
          camera = entity;
        }
      }
      REQUIRE(character.is_valid());
      REQUIRE(camera.is_valid());
      const float dt = 1.0f / static_cast<float>(rate);
      const auto frames = [&](float seconds) {
        for (int i = 0; i < static_cast<int>(std::lround(seconds * static_cast<float>(rate))); ++i) {
          game.update(dt);
        }
      };
      const auto press = [&](platform::Key key) {
        game.event(platform::KeyPressed{.key = key, .modifiers = {}, .repeat = false});
      };
      const auto release = [&](platform::Key key) { game.event(platform::KeyReleased{.key = key, .modifiers = {}}); };
      const auto position = [&] { return character.get<world::Transform>().position; };

      // To the open floor south of the course, west of the columns; the camera looks north, so D
      // runs east along it. Writing the Transform teleports the character.
      frames(0.5f);
      world::Transform start = character.get<world::Transform>();
      start.position = {-6.0f, 0.0f, 11.0f};
      character.set(start);
      frames(0.5f);
      REQUIRE(game.physics().character(character)->grounded);
      // Facing east first: from standing facing north, D is a quarter turn on the spot, which holds
      // the run back (issue #528). A short press starts it without moving; the run below then starts
      // facing the way it goes.
      press(platform::Key::D);
      frames(0.05f);
      release(platform::Key::D);
      frames(2.0f);

      const float x = position().x;
      press(platform::Key::D);
      std::vector<glm::vec3> eye;
      for (int i = 0; i < 2 * rate; ++i) {
        game.update(dt);
        if (i >= rate) { // the second second: at the run speed, the camera caught up
          eye.push_back(camera.get<world::Transform>().position);
        }
      }
      result.travel = position().x - x;
      release(platform::Key::D);
      std::vector<float> moved;
      for (std::size_t i = 1; i < eye.size(); ++i) {
        moved.push_back(glm::length(eye[i] - eye[i - 1]));
      }
      const float mean = std::accumulate(moved.begin(), moved.end(), 0.0f) / static_cast<float>(moved.size());
      for (const float distance : moved) {
        result.judder = std::max(result.judder, std::abs(distance - mean) / mean);
      }

      frames(1.0f);
      const float ground = position().y;
      press(platform::Key::Space);
      game.update(dt);
      release(platform::Key::Space);
      float highest = ground;
      for (int i = 0; i < rate * 3 / 2; ++i) {
        game.update(dt);
        highest = std::max(highest, position().y);
      }
      result.height = highest - ground;
    }
    core::Log::removeSink(problems);
    CHECK(problems->problems.empty());
    return result;
  };

  const Result at60 = play(60);
  for (const int rate : {30, 60, 144}) {
    const Result result = rate == 60 ? at60 : play(rate);
    INFO(rate << " frames a second: " << result.travel << " m in 2 s, a jump " << result.height
              << " m high, the camera's motion within " << result.judder * 100.0f << "% of its mean");
    // 4.8 m/s for two seconds, less the 0.96 m reaching it at 12 m/s^2 costs and the step the
    // first one to read the key takes: 8.52 m measured.
    CHECK(result.travel == Approx(8.6f).margin(0.15f));
    // At 30 and 60 a press lands on a step's start; at 144 it can land up to one step before the
    // next one reads it, which is at most 4.8 m/s * 1/60 s = 0.08 m less of running.
    CHECK(result.travel == Approx(at60.travel).margin(0.09f));
    // 5 m/s up is 1.27 m in closed form; Jolt's steps of 1/60 s reach 1.23 m at every rate.
    CHECK(result.height == Approx(1.25f).margin(0.05f));
    CHECK(result.height == Approx(at60.height).margin(0.01f));
    CHECK(result.judder < 0.01f); // measured at 0.01 to 0.014 %
  }
}

namespace {

// A project of two empty scenes, for what needs no entity.
std::filesystem::path emptyProject(const std::string &name) {
  const std::filesystem::path root = std::filesystem::temp_directory_path() / name;
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "scenes");
  assets::Project project;
  project.root = root;
  project.name = "Empty";
  project.startScene = "scenes/main.scene.json";
  REQUIRE(project.save().has_value());
  for (const char *scene : {"main", "other"}) {
    REQUIRE(core::writeFile(root / "scenes" / std::format("{}.scene.json", scene),
                            std::string_view{R"({"version": 3, "entities": []})"})
                .has_value());
  }
  return root;
}

const platform::Event EscapeKey =
    platform::KeyPressed{.key = platform::Key::Escape, .modifiers = platform::Modifiers::None, .repeat = false};
const platform::Event LeftClick =
    platform::MouseButtonPressed{.button = platform::MouseButton::Left, .position = {10.0f, 10.0f}, .clicks = 1};

} // namespace

TEST_CASE("the player locks the pointer for a script while it has the focus and lets it go",
          "[runtime][gpu][mouselock]") {
  Fixture fixture;
  const std::filesystem::path root = emptyProject("sonnet_runtime_mouse_lock");
  test::RelativeModeWindow window{*fixture.window};
  {
    runtime::Game game{window, *fixture.device, *fixture.swapchain, fixture.desc()};
    REQUIRE(game.open(root).has_value());
    game.update(1.0f / 60.0f);
    REQUIRE_FALSE(window.relativeMouseMode());

    // The request is applied at the next update, and the state the scripts read follows.
    REQUIRE(game.scripts().run("input.setMouseLocked(true)", "lock").has_value());
    game.update(1.0f / 60.0f);
    REQUIRE(window.relativeMouseMode());
    REQUIRE(game.input().mouseLocked());

    // Escape lets go and is not the game's; a click takes it back and is not the game's either.
    game.event(EscapeKey);
    REQUIRE_FALSE(window.relativeMouseMode());
    REQUIRE_FALSE(game.input().keyPressed(platform::Key::Escape));
    game.update(1.0f / 60.0f);
    REQUIRE_FALSE(game.input().mouseLocked());
    game.event(LeftClick);
    REQUIRE_FALSE(game.input().mousePressed(platform::MouseButton::Left));
    game.update(1.0f / 60.0f);
    REQUIRE(window.relativeMouseMode());
    REQUIRE(game.input().mouseLocked());

    // A lost focus lets go, and the focus coming back waits for a click.
    game.event(platform::WindowFocusChanged{.focused = false});
    REQUIRE_FALSE(window.relativeMouseMode());
    game.event(platform::WindowFocusChanged{.focused = true});
    game.update(1.0f / 60.0f);
    REQUIRE_FALSE(game.input().mouseLocked());
    game.event(LeftClick);
    game.update(1.0f / 60.0f);
    REQUIRE(game.input().mouseLocked());

    // A replaced scene takes the request with its scripts.
    REQUIRE(game.openScene("scenes/other.scene.json").has_value());
    REQUIRE_FALSE(window.relativeMouseMode());
    REQUIRE_FALSE(game.input().mouseLocked());
    game.update(1.0f / 60.0f);
    REQUIRE_FALSE(window.relativeMouseMode());

    // The game going away gives the pointer back.
    REQUIRE(game.scripts().run("input.setMouseLocked(true)", "lock").has_value());
    game.update(1.0f / 60.0f);
    REQUIRE(window.relativeMouseMode());
  }
  REQUIRE_FALSE(window.relativeMouseMode());
  sonnet::test::removeScratch(root);
}

TEST_CASE("a window without relative mode leaves the player's pointer unlocked and the clicks the game's",
          "[runtime][gpu][mouselock]") {
  Fixture fixture;
  const std::filesystem::path root = emptyProject("sonnet_runtime_mouse_lock_refused");
  test::RelativeModeWindow window{*fixture.window};
  window.supported = false;
  {
    runtime::Game game{window, *fixture.device, *fixture.swapchain, fixture.desc()};
    REQUIRE(game.open(root).has_value());
    REQUIRE(game.scripts().run("input.setMouseLocked(true)", "lock").has_value());
    for (int frame = 0; frame < 5; ++frame) {
      game.update(1.0f / 60.0f);
    }
    REQUIRE_FALSE(game.input().mouseLocked());
    REQUIRE(window.requests == 1);
    REQUIRE(game.scripts().run("assert(input.mouseLocked() == false)", "check").has_value());
    game.event(LeftClick);
    REQUIRE(game.input().mousePressed(platform::MouseButton::Left));
  }
  sonnet::test::removeScratch(root);
}
