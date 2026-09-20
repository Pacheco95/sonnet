#include <sonnet/runtime/Game.h>

#include <sonnet/assets/Cook.h>
#include <sonnet/core/Error.h>
#include <sonnet/core/File.h>
#include <sonnet/core/Log.h>
#include <sonnet/physics/Components.h>
#include <sonnet/platform/Platform.h>
#include <sonnet/rhi/Device.h>
#include <sonnet/rhi/Swapchain.h>
#include <sonnet/world/Components.h>

#include <catch2/catch_test_macros.hpp>

#include <spdlog/sinks/base_sink.h>

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
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
  }
};

} // namespace

TEST_CASE("the player runs a project folder's start scene", "[runtime][gpu]") {
  Fixture fixture;
  const std::filesystem::path project = sampleProject(fixture.platform);
  if (project.empty()) {
    SKIP("the basic sample was not found in a checkout above the test binary");
  }

  const auto sink = std::make_shared<ProblemSink>();
  core::Log::addSink(sink);
  runtime::Game game{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain, fixture.desc()};
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
    runtime::Game game{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain, fixture.desc()};
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
    runtime::Game game{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain, fixture.desc()};
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

  runtime::Game game{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain, fixture.desc()};
  REQUIRE(game.open(root).has_value());
  fixture.frame(game);
  // The fallback looks at the origin from up and to the side, so it is not the identity.
  REQUIRE(game.camera().position.y > 0.0f);
  fixture.frame(game); // warned once, not every frame
  std::filesystem::remove_all(root);
}

TEST_CASE("a path that is neither a project nor a bundle is an error", "[runtime][gpu]") {
  Fixture fixture;
  runtime::Game game{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain, fixture.desc()};
  REQUIRE(!game.open(std::filesystem::temp_directory_path() / "sonnet_runtime_nowhere").has_value());
  REQUIRE(!game.open(std::filesystem::temp_directory_path() / "sonnet_runtime_nowhere.sbundle").has_value());
  // A failed open still leaves a game that renders an empty frame rather than crashing.
  fixture.frame(game);
}
