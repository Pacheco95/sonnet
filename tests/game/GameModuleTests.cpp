#include <sonnet/core/Error.h>
#include <sonnet/core/Log.h>
#include <sonnet/editor/Editor.h>
#include <sonnet/game/LinkedGame.h>
#include <sonnet/gui/Components.h>
#include <sonnet/platform/Platform.h>
#include <sonnet/rhi/Device.h>
#include <sonnet/rhi/Swapchain.h>
#include <sonnet/runtime/Game.h>
#include <sonnet/world/Components.h>
#include <sonnet/world/World.h>

#include <catch2/catch_test_macros.hpp>

#include <spdlog/sinks/base_sink.h>

#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

using namespace sonnet;

namespace {

constexpr int Frames = 90;
constexpr float Step = 1.0f / 60.0f;

struct Fixture {
  platform::Platform platform{{.headless = true}};
  std::unique_ptr<platform::IWindow> window;
  std::unique_ptr<rhi::IDevice> device;
  std::unique_ptr<rhi::ISwapchain> swapchain;

  Fixture() {
    try {
      window = platform.createWindow({.title = "game_module_tests", .size = {800, 600}});
      device = rhi::createDevice({.platform = &platform, .applicationName = "game_module_tests"});
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

  template <typename App> void frame(App &app) {
    app.update(Step);
    rhi::ICommandList &commands = device->beginFrame();
    const auto image = swapchain->acquire();
    REQUIRE(image.has_value());
    app.render(commands, image);
    device->endFrame();
    app.afterPresent();
  }
};

// A copy of the project, so the checkout gets no cache or sidecars from the test.
[[nodiscard]] std::filesystem::path copyProject(const char *name) {
  const std::filesystem::path directory = std::filesystem::temp_directory_path() / "sonnet_game_module_tests" / name;
  std::filesystem::remove_all(directory);
  std::filesystem::create_directories(directory.parent_path());
  std::filesystem::copy(SONNET_GAME_PROJECT, directory, std::filesystem::copy_options::recursive);
  return directory;
}

// What the two runs are compared on: every entity's position, by name, and every label's text.
struct Observed {
  std::map<std::string, glm::vec3> positions;
  std::map<std::string, std::string> texts;
};

[[nodiscard]] Observed observe(world::World &world) {
  Observed observed;
  world.ecs().each([&](flecs::entity entity, const world::Name &name, const world::Transform &transform) {
    observed.positions[name.value] = transform.position;
    if (const gui::GuiText *text = entity.try_get<gui::GuiText>()) {
      observed.texts[name.value] = text->text;
    }
  });
  return observed;
}

// Collects warnings and errors: a scene that names a component nobody registered warns.
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

} // namespace

TEST_CASE("the game module plays the same in the player and in the editor", "[game][gpu]") {
  const runtime::GameModule *module = game::linkedModule();
  REQUIRE(module != nullptr);
  Fixture fixture;
  const auto sink = std::make_shared<ProblemSink>();
  core::Log::addSink(sink);

  Observed initial;
  Observed played;
  {
    runtime::GameDesc desc;
    desc.audioOutput = false;
    desc.module = module;
    runtime::Game game{*fixture.window, *fixture.device, *fixture.swapchain, desc};
    const auto opened = game.open(copyProject("player"));
    REQUIRE(opened.has_value());
    initial = observe(game.world());
    for (int i = 0; i < Frames; ++i) {
      fixture.frame(game);
    }
    played = observe(game.world());
  }

  Observed edited;
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain, false, module};
    const auto opened = editor.openProject(copyProject("editor"));
    REQUIRE(opened.has_value());
    // The module's component loaded with the scene, which a generic editor would have dropped.
    REQUIRE(observe(editor.world()).positions == initial.positions);
    editor.play();
    for (int i = 0; i < Frames; ++i) {
      fixture.frame(editor);
    }
    edited = observe(editor.world());
    editor.stop();
  }
  core::Log::removeSink(sink);
  REQUIRE(sink->problems.empty());

  // The module's system moved something and its script showed it, through the native binding.
  REQUIRE(played.positions != initial.positions);
  REQUIRE(!played.texts.empty());
  for (const auto &[name, text] : played.texts) {
    INFO(name << ": " << text);
    REQUIRE(text.find("period") != std::string::npos);
  }
  // And the editor's play mode is what ships.
  REQUIRE(edited.positions == played.positions);
  REQUIRE(edited.texts == played.texts);
}
