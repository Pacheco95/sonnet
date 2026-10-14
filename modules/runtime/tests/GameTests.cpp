#include <sonnet/runtime/Capture.h>
#include <sonnet/runtime/Game.h>

#include <sonnet/assets/Bundle.h>
#include <sonnet/assets/Cook.h>
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

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <spdlog/sinks/base_sink.h>

#include <stb_image.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

using namespace sonnet;
using Catch::Approx;

namespace {

// The sample the editor's play-mode tests use too: a real project with models, a skin, a clip,
// sounds, scripts and physics. Found in the checkout a build directory lives in, the way the
// log panel's source links are (docs/editor.md, "Projects and scenes").
[[nodiscard]] std::filesystem::path sampleProject(platform::Platform &platform, std::string_view name = "basic") {
  std::error_code error;
  for (std::filesystem::path base = std::filesystem::absolute(platform.basePath(), error); !base.empty();
       base = base.parent_path()) {
    const std::filesystem::path candidate = base / "apps" / "samples" / name;
    if (std::filesystem::is_regular_file(candidate / "project.json", error)) {
      return candidate.lexically_normal();
    }
    if (base == base.root_path()) {
      break;
    }
  }
  return {};
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

// The player's capture flags, parsed as the player parses them, with the output resolved as it
// resolves it: under the preferences directory.
[[nodiscard]] runtime::CaptureOptions playerCapture(platform::Platform &platform,
                                                    std::initializer_list<std::string_view> flags) {
  const std::vector<std::string_view> args{flags};
  const auto line = runtime::parseCommandLine(args, runtime::CaptureApplication::Player);
  REQUIRE(line.has_value());
  REQUIRE(line->capture.has_value());
  runtime::CaptureOptions options = *line->capture;
  runtime::resolveOutputs(options, platform.prefPath("sonnet", "runtime_tests"));
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

  const std::filesystem::path written = fixture.platform.prefPath("sonnet", "runtime_tests") / "shots" / "gui.png";
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
  std::filesystem::remove_all(out);
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
  std::filesystem::remove_all(root);
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
  const runtime::CaptureOptions options =
      playerCapture(fixture.platform, {"--play", "0.25", "--shading-term", "albedo", "--settle-frames", "2",
                                       "--screenshot", "shots/albedo.png"});
  const std::filesystem::path written = fixture.platform.prefPath("sonnet", "runtime_tests") / "shots" / "albedo.png";
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
  const runtime::CaptureOptions options = playerCapture(
      fixture.platform, {"--settle-frames", "2", "--render-size", "96x64", "--screenshot", "shots/sized.png"});
  REQUIRE(options.renderSize == glm::uvec2{96, 64});
  const std::filesystem::path written = fixture.platform.prefPath("sonnet", "runtime_tests") / "shots" / "sized.png";
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
    runtime::CaptureRun run{playerCapture(fixture.platform, {"--scene", "scenes/playground.scene.json",
                                                             "--settle-frames", "1", "--screenshot", shot.string()})};
    REQUIRE(fixture.capture(game, run) == runtime::CaptureRun::Status::Done);
    REQUIRE(hasBall());
    REQUIRE_FALSE(game.world().isPlaying()); // no --play: the scene as it loads, as in the editor
  }
  REQUIRE(std::filesystem::is_regular_file(shot));

  // A scene the bundle does not hold fails the run, with nothing written.
  {
    runtime::Game game{*fixture.window, *fixture.device, *fixture.swapchain, desc};
    REQUIRE(game.open(out / "game.sbundle").has_value());
    runtime::CaptureRun run{playerCapture(
        fixture.platform, {"--scene", "scenes/nowhere.scene.json", "--screenshot", (out / "no.png").string()})};
    REQUIRE(fixture.capture(game, run) == runtime::CaptureRun::Status::Failed);
  }
  REQUIRE_FALSE(std::filesystem::exists(out / "no.png"));
  std::filesystem::remove_all(out);
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
  std::filesystem::remove_all(root);
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
  std::filesystem::remove_all(out);
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
    REQUIRE(changed(world::saveScene(world)) == changed(authored));
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
  std::filesystem::remove_all(out);
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

    // Three seconds in the walker crossfades to Walk: the idle clip is a layer for 0.4 s.
    for (int i = 0; i < 125; ++i) {
      game.update(1.0f / 60.0f);
    }
    REQUIRE(walker.get<world::Animator>().clip != idle);
    REQUIRE(walker.get<world::Animator>().layers.size() == 1);
    for (int i = 0; i < 60; ++i) {
      game.update(1.0f / 60.0f);
    }
    REQUIRE(walker.get<world::Animator>().layers.empty());
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
  std::filesystem::remove_all(out);
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
  const runtime::CaptureOptions options = playerCapture(
      fixture.platform, {"--play", "1", "--settle-frames", "12", "--screenshot", std::format("golden/{}.png", name)});
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
  const runtime::CaptureOptions options =
      playerCapture(fixture.platform, {"--settle-frames", "6", "--screenshot", "golden/menu.png"});
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
    const runtime::CaptureOptions options =
        playerCapture(fixture.platform, {"--settle-frames", "1", "--screenshot", name});
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
  std::filesystem::remove_all(root);
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
