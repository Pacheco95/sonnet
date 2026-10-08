#include <sonnet/runtime/Capture.h>
#include <sonnet/runtime/Game.h>

#include <sonnet/assets/Cook.h>
#include <sonnet/core/Error.h>
#include <sonnet/core/File.h>
#include <sonnet/core/Log.h>
#include <sonnet/physics/Components.h>
#include <sonnet/platform/Platform.h>
#include <sonnet/rhi/Device.h>
#include <sonnet/rhi/NullDevice.h>
#include <sonnet/rhi/Swapchain.h>
#include <sonnet/world/Components.h>

#include <catch2/catch_test_macros.hpp>

#include <spdlog/sinks/base_sink.h>

#include <stb_image.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

using namespace sonnet;

namespace {

// The sample the editor's play-mode tests use too: a real project with models, a skin, a clip,
// sounds, scripts and physics. Found in the checkout a build directory lives in, the way the
// log panel's source links are (docs/editor.md, "Projects and scenes").
[[nodiscard]] std::filesystem::path sampleProject(platform::Platform &platform) {
  std::error_code error;
  for (std::filesystem::path base = std::filesystem::absolute(platform.basePath(), error); !base.empty();
       base = base.parent_path()) {
    const std::filesystem::path candidate = base / "apps" / "samples" / "basic";
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

  Fixture() {
    try {
      window = platform.createWindow({.title = "runtime_tests", .size = {640, 480}});
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
                         .antialiasing = false,
                         .environmentSize = 8,
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
