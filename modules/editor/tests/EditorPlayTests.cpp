#include <sonnet/editor/Editor.h>
#include <sonnet/editor/Preferences.h>

#include <sonnet/core/Error.h>
#include <sonnet/core/Log.h>
#include <sonnet/physics/Components.h>
#include <sonnet/platform/Event.h>
#include <sonnet/platform/Platform.h>
#include <sonnet/rhi/Device.h>
#include <sonnet/rhi/Swapchain.h>
#include <sonnet/scripting/Components.h>
#include <sonnet/world/Components.h>

#include <catch2/catch_test_macros.hpp>

#include <spdlog/sinks/base_sink.h>

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

using namespace sonnet;

namespace {

struct Fixture {
  platform::Platform platform{{.headless = true}};
  std::unique_ptr<platform::IWindow> window;
  std::unique_ptr<rhi::IDevice> device;
  std::unique_ptr<rhi::ISwapchain> swapchain;

  Fixture() {
    try {
      window = platform.createWindow({.title = "editor_tests", .size = {800, 600}});
      device = rhi::createDevice({.platform = &platform, .applicationName = "editor_tests"});
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

  void frame(editor::Editor &editor) {
    editor.update(1.0f / 60.0f);
    rhi::ICommandList &commands = device->beginFrame();
    const auto image = swapchain->acquire();
    REQUIRE(image.has_value());
    editor.render(commands, image);
    device->endFrame();
    editor.afterPresent();
  }
};

flecs::entity byName(world::World &world, std::string_view name) {
  for (const flecs::entity root : world.roots()) {
    if (root.get<world::Name>().value == name) {
      return root;
    }
  }
  FAIL("no entity " << name);
  return {};
}

// Keeps the warnings and errors logged while it is registered.
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

// Launches its entity upwards on the first fixed step and marks it with a spin.
constexpr std::string_view Launcher = R"lua(
local Launcher = {}
function Launcher:start()
  self.entity:set("Spin", { speed = 3 })
end
function Launcher:fixedUpdate(dt)
  if not self.launched then
    physics.addImpulse(self.entity, vec3(0, 8, 0))
    self.launched = true
  end
end
return Launcher
)lua";

} // namespace

TEST_CASE("play mode runs physics and scripts and stop puts everything back", "[editor][gpu]") {
  Fixture fixture;
  const std::filesystem::path directory = std::filesystem::temp_directory_path() / "sonnet_editor_tests" / "play";
  std::filesystem::remove_all(directory);
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    REQUIRE(editor.createProject(directory, "Play").has_value());
    world::World &world = editor.world();
    const auto script = editor.assets().createScript(directory / "scripts" / "launcher.lua", Launcher);
    REQUIRE(script.has_value());
    // The ground collides through its plane mesh; the box is a scripted dynamic body.
    byName(world, "Ground").set<physics::MeshCollider>({});
    const flecs::entity box = byName(world, "Box");
    const core::Uuid boxUuid = world.uuidOf(box);
    box.set<physics::BoxCollider>({});
    box.set<physics::RigidBody>({});
    box.set<scripting::Script>({.script = *script});
    editor.setShowColliders(true);
    fixture.frame(editor);
    REQUIRE(editor.physics().bodyCount() == 0);

    editor.play();
    for (int frame = 0; frame < 30; ++frame) {
      fixture.frame(editor);
    }
    const flecs::entity running = world.find(boxUuid);
    REQUIRE(editor.scripts().instanceCount() == 1);
    REQUIRE(editor.physics().bodyCount() == 2);
    REQUIRE(running.get<world::Spin>().speed == 3.0f);
    REQUIRE(running.get<world::Transform>().position.y > 1.5f);
    // Game input stays empty while the viewport does not have the focus.
    editor.event(platform::KeyPressed{.key = platform::Key::Space, .modifiers = {}, .repeat = false});
    REQUIRE(!editor.gameInput().keyDown(platform::Key::Space));

    editor.stop();
    const flecs::entity restored = world.find(boxUuid);
    REQUIRE(restored.get<world::Transform>().position.y == 0.5f);
    REQUIRE(!restored.has<world::Spin>());
    REQUIRE(editor.scripts().instanceCount() == 0);
    REQUIRE(editor.physics().bodyCount() == 0);
    fixture.frame(editor);

    // A second run starts from the same state.
    editor.play();
    for (int frame = 0; frame < 30; ++frame) {
      fixture.frame(editor);
    }
    REQUIRE(world.find(boxUuid).get<world::Transform>().position.y > 1.5f);
    editor.stop();
    fixture.frame(editor);
  }
  fixture.device->waitIdle();
  REQUIRE(fixture.device->validationMessageCount() == 0);
  std::filesystem::remove_all(directory);
}

TEST_CASE("the basic sample's playground plays its scripts and physics and resets on stop", "[editor][gpu][samples]") {
  Fixture fixture;
  const std::optional<std::filesystem::path> manifest =
      editor::locateSource("apps/samples/basic/project.json", {}, fixture.platform.basePath());
  if (!manifest) {
    SKIP("the sample was not found above " << fixture.platform.basePath().string());
  }
  // A copy, so the checkout's sample gets no cache or sidecars from the test.
  const std::filesystem::path directory = std::filesystem::temp_directory_path() / "sonnet_editor_tests" / "basic";
  std::filesystem::remove_all(directory);
  std::filesystem::create_directories(directory.parent_path());
  std::filesystem::copy(manifest->parent_path(), directory, std::filesystem::copy_options::recursive);
  const auto sink = std::make_shared<ProblemSink>();
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    REQUIRE(editor.openProject(directory).has_value());
    REQUIRE(editor.openScene(directory / "scenes" / "playground.scene.json").has_value());
    world::World &world = editor.world();
    const auto crates = [&] {
      return std::ranges::count_if(
          world.roots(), [&](flecs::entity root) { return root.get<world::Name>().value == "Physics crate"; });
    };
    const std::size_t entities = world.roots().size();
    const core::Uuid ball = world.uuidOf(byName(world, "Ball"));
    const core::Uuid top = world.uuidOf(byName(world, "Stacked crate 6"));
    core::Log::addSink(sink);

    editor.play();
    for (int frame = 0; frame < 200; ++frame) {
      fixture.frame(editor);
    }
    REQUIRE(sink->problems.empty());
    REQUIRE(editor.scripts().instanceCount() == 4);
    REQUIRE(crates() == 2); // one every 1.5 s
    // The ball rests on the floor, and the pyramid's top crate still stands on the others.
    REQUIRE(world.find(ball).get<world::Transform>().position.y < 0.6f);
    REQUIRE(world.find(top).get<world::Transform>().position.y > 2.3f);

    editor.stop();
    fixture.frame(editor);
    REQUIRE(crates() == 0);
    REQUIRE(world.roots().size() == entities);
    REQUIRE(editor.scripts().instanceCount() == 0);
    REQUIRE(sink->problems.empty());
    core::Log::removeSink(sink);
  }
  fixture.device->waitIdle();
  REQUIRE(fixture.device->validationMessageCount() == 0);
  std::filesystem::remove_all(directory);
}
