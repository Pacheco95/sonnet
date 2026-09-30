#include <sonnet/editor/Editor.h>
#include <sonnet/editor/EntityCommands.h>
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

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <spdlog/sinks/base_sink.h>

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
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
    editor.event(platform::TouchDown{.id = 1, .position = {10.0f, 10.0f}});
    REQUIRE(editor.gameInput().touches().empty());

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

TEST_CASE("an edit made before play can be undone after stop, and the play's own edits are gone", "[editor][gpu]") {
  Fixture fixture;
  editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
  world::World &world = editor.world();
  const flecs::entity box = byName(world, "Box");
  const core::Uuid uuid = world.uuidOf(box);
  editor.commands().push(editor::renameCommand(uuid, "Box", "Before"), world);
  REQUIRE(editor.isDirty());
  fixture.frame(editor);

  editor.play();
  REQUIRE(editor.isDirty()); // the edit before play is still unsaved
  REQUIRE(!editor.commands().canUndo());
  editor.commands().push(editor::renameCommand(uuid, "Before", "During"), world);
  fixture.frame(editor);
  editor.stop();
  fixture.frame(editor);

  REQUIRE(world.find(uuid).get<world::Name>().value == "Before");
  REQUIRE(editor.commands().size() == 1);
  REQUIRE(editor.isDirty());
  REQUIRE(editor.commands().undo(world));
  REQUIRE(world.find(uuid).get<world::Name>().value == "Box");
  REQUIRE(!editor.isDirty());
  REQUIRE(editor.commands().redo(world));
  REQUIRE(world.find(uuid).get<world::Name>().value == "Before");
}

namespace {

// A component command on the box's Transform with the position set to `after`, made the way the
// inspector makes one mid-play: `before` is whatever the running scene holds at that moment.
std::unique_ptr<editor::ICommand> moveBox(world::World &world, core::Uuid uuid, glm::vec3 after) {
  const flecs::entity box = world.find(uuid);
  const flecs::entity_t id = world.findComponent("Transform")->id;
  const nlohmann::json before = world.componentToJson(box, id);
  world::Transform edited = box.get<world::Transform>();
  edited.position = after;
  box.set<world::Transform>(edited);
  return editor::componentCommand(uuid, "Transform", std::optional{before},
                                  std::optional{world.componentToJson(box, id)}, "move Box");
}

// Moves the box the way physics does, with no command behind it.
void simulateBoxMoving(world::World &world, core::Uuid uuid) {
  world.find(uuid).set<world::Transform>({.position = {9.0f, 9.0f, 9.0f}});
}

} // namespace

TEST_CASE("stopping a play that nothing edited needs no dialog however much moved", "[editor][gpu][changes]") {
  Fixture fixture;
  editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
  world::World &world = editor.world();
  const core::Uuid uuid = world.uuidOf(byName(world, "Box"));
  editor.play();
  simulateBoxMoving(world, uuid);
  REQUIRE(editor.playChanges().empty());
  editor.requestStop();
  REQUIRE_FALSE(editor.stopPromptOpen());
  REQUIRE_FALSE(editor.isPlaying());
  REQUIRE(world.find(uuid).get<world::Transform>().position == glm::vec3{0.0f, 0.5f, 0.0f});
}

TEST_CASE("an edit that was undone during play is not a change", "[editor][gpu][changes]") {
  Fixture fixture;
  editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
  world::World &world = editor.world();
  const core::Uuid uuid = world.uuidOf(byName(world, "Box"));
  editor.play();
  editor.commands().push(editor::renameCommand(uuid, "Box", "Renamed"), world);
  REQUIRE(editor.playChanges().size() == 1);
  REQUIRE(editor.commands().undo(world));
  REQUIRE(editor.playChanges().empty());
  editor.requestStop();
  REQUIRE_FALSE(editor.stopPromptOpen());
  REQUIRE_FALSE(editor.isPlaying());
}

TEST_CASE("Keep re-applies the play's edits to the restored scene, unsaved and undoable", "[editor][gpu][changes]") {
  Fixture fixture;
  editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
  world::World &world = editor.world();
  const core::Uuid uuid = world.uuidOf(byName(world, "Box"));
  REQUIRE_FALSE(editor.isDirty());

  editor.play();
  simulateBoxMoving(world, uuid);
  editor.commands().push(moveBox(world, uuid, {1.0f, 2.0f, 3.0f}), world);
  editor.commands().push(editor::renameCommand(uuid, "Box", "Crate"), world);
  fixture.frame(editor);

  editor.requestStop();
  REQUIRE(editor.stopPromptOpen());
  REQUIRE(editor.isPlaying());
  REQUIRE(editor.playChanges() == std::vector<std::string>{"move Box", "rename Box to Crate"});

  editor.keepPlayChanges();
  REQUIRE_FALSE(editor.isPlaying());
  REQUIRE_FALSE(editor.stopPromptOpen());
  REQUIRE(editor.skippedChanges() == 0);
  REQUIRE(world.find(uuid).get<world::Transform>().position == glm::vec3{1.0f, 2.0f, 3.0f});
  REQUIRE(world.find(uuid).get<world::Name>().value == "Crate");
  REQUIRE(editor.isDirty());
  REQUIRE(editor.commands().size() == 2);

  // Undo puts back what the restored scene held, not where physics had taken the box.
  REQUIRE(editor.commands().undo(world));
  REQUIRE(world.find(uuid).get<world::Name>().value == "Box");
  REQUIRE(editor.commands().undo(world));
  REQUIRE(world.find(uuid).get<world::Transform>().position == glm::vec3{0.0f, 0.5f, 0.0f});
  REQUIRE_FALSE(editor.isDirty());
}

TEST_CASE("Discard restores the scene as it was, and Cancel plays on", "[editor][gpu][changes]") {
  Fixture fixture;
  editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
  world::World &world = editor.world();
  const core::Uuid uuid = world.uuidOf(byName(world, "Box"));
  editor.commands().push(editor::renameCommand(uuid, "Box", "Before"), world);

  editor.play();
  editor.commands().push(moveBox(world, uuid, {4.0f, 4.0f, 4.0f}), world);
  editor.requestStop();
  REQUIRE(editor.stopPromptOpen());
  editor.cancelStop();
  REQUIRE_FALSE(editor.stopPromptOpen());
  REQUIRE(editor.isPlaying());
  REQUIRE(editor.playChanges().size() == 1);
  REQUIRE(world.find(uuid).get<world::Transform>().position == glm::vec3{4.0f, 4.0f, 4.0f});

  editor.requestStop();
  editor.discardPlayChanges();
  REQUIRE_FALSE(editor.isPlaying());
  REQUIRE_FALSE(editor.stopPromptOpen());
  REQUIRE(world.find(uuid).get<world::Transform>().position == glm::vec3{0.0f, 0.5f, 0.0f});
  REQUIRE(editor.commands().size() == 1); // the history from before play, nothing from it
  REQUIRE(editor.isDirty());
}

TEST_CASE("Save as writes the edited scene and leaves the open scene unchanged", "[editor][gpu][changes]") {
  Fixture fixture;
  const std::filesystem::path directory = std::filesystem::temp_directory_path() / "sonnet_editor_tests" / "keep_as";
  std::filesystem::remove_all(directory);
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    REQUIRE(editor.createProject(directory, "KeepAs").has_value());
    world::World &world = editor.world();
    const core::Uuid uuid = world.uuidOf(byName(world, "Box"));
    REQUIRE_FALSE(editor.isDirty());

    editor.play();
    simulateBoxMoving(world, uuid);
    editor.commands().push(moveBox(world, uuid, {1.0f, 2.0f, 3.0f}), world);
    editor.requestStop();
    REQUIRE(editor.stopPromptOpen());

    // A path that cannot be written, and the open scene's own file, leave play running.
    REQUIRE_FALSE(editor.savePlayChangesAs(directory).has_value());
    REQUIRE_FALSE(editor.savePlayChangesAs(directory / "scenes" / "main.scene.json").has_value());
    REQUIRE(editor.isPlaying());
    REQUIRE(editor.stopPromptOpen());
    REQUIRE_FALSE(editor.modalError().empty());

    const std::filesystem::path file = directory / "scenes" / "edited.scene.json";
    REQUIRE(editor.savePlayChangesAs(file).has_value());
    REQUIRE_FALSE(editor.isPlaying());
    REQUIRE_FALSE(editor.stopPromptOpen());
    REQUIRE(std::filesystem::exists(file));
    REQUIRE(world.find(uuid).get<world::Transform>().position == glm::vec3{0.0f, 0.5f, 0.0f});
    REQUIRE_FALSE(editor.isDirty());
    REQUIRE(editor.commands().size() == 0);

    // The file holds the edit: opening it shows the box where the edit put it.
    REQUIRE(editor.openScene(file).has_value());
    REQUIRE(editor.tabCount() == 2);
    REQUIRE(byName(editor.world(), "Box").get<world::Transform>().position == glm::vec3{1.0f, 2.0f, 3.0f});
  }
  fixture.device->waitIdle();
  std::filesystem::remove_all(directory);
}

TEST_CASE("an edit to an entity the play spawned is skipped and does not fail the stop", "[editor][gpu][changes]") {
  Fixture fixture;
  editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
  world::World &world = editor.world();
  const core::Uuid uuid = world.uuidOf(byName(world, "Box"));

  editor.play();
  const core::Uuid spawned = core::Uuid::generate();
  world.createEntity("Spawned", {}, spawned); // what a script's spawn does: no command behind it
  editor.commands().push(editor::renameCommand(spawned, "Spawned", "Renamed"), world);
  editor.commands().push(editor::renameCommand(uuid, "Box", "Crate"), world);
  editor.requestStop();
  editor.keepPlayChanges();

  REQUIRE_FALSE(editor.isPlaying());
  REQUIRE(editor.skippedChanges() == 1);
  REQUIRE(editor.stopPromptOpen()); // stays up to say so
  REQUIRE_FALSE(world.find(spawned));
  REQUIRE(world.find(uuid).get<world::Name>().value == "Crate");
  REQUIRE(editor.commands().size() == 1);
}

TEST_CASE("quitting while playing with edits goes through the stop dialog first", "[editor][gpu][changes]") {
  Fixture fixture;
  editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
  world::World &world = editor.world();
  const core::Uuid uuid = world.uuidOf(byName(world, "Box"));

  editor.play();
  editor.commands().push(editor::renameCommand(uuid, "Box", "Crate"), world);
  editor.requestQuit();
  REQUIRE(editor.stopPromptOpen());
  REQUIRE(editor.isPlaying());
  REQUIRE_FALSE(editor.quitRequested());

  editor.cancelStop();
  REQUIRE(editor.isPlaying());
  REQUIRE_FALSE(editor.quitRequested());

  editor.requestQuit();
  editor.discardPlayChanges();
  REQUIRE_FALSE(editor.isPlaying());
  REQUIRE(editor.quitRequested()); // the scene is clean again, so nothing else to ask
}

TEST_CASE("Keep before a quit leaves the unsaved scene for the quit dialog", "[editor][gpu][changes]") {
  Fixture fixture;
  editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
  world::World &world = editor.world();
  const core::Uuid uuid = world.uuidOf(byName(world, "Box"));
  editor.play();
  editor.commands().push(editor::renameCommand(uuid, "Box", "Crate"), world);
  editor.requestQuit();
  editor.keepPlayChanges();
  REQUIRE_FALSE(editor.stopPromptOpen());
  REQUIRE(editor.quitPromptOpen());
  REQUIRE_FALSE(editor.quitRequested());
}

TEST_CASE("pause freezes play mode, edits land on the frozen scene, stop from pause restores", "[editor][gpu][pause]") {
  Fixture fixture;
  const std::filesystem::path directory = std::filesystem::temp_directory_path() / "sonnet_editor_tests" / "pause";
  std::filesystem::remove_all(directory);
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    REQUIRE(editor.createProject(directory, "Pause").has_value());
    world::World &world = editor.world();
    const auto script = editor.assets().createScript(directory / "scripts" / "launcher.lua", Launcher);
    REQUIRE(script.has_value());
    byName(world, "Ground").set<physics::MeshCollider>({});
    const flecs::entity box = byName(world, "Box");
    const core::Uuid boxUuid = world.uuidOf(box);
    box.set<physics::BoxCollider>({});
    box.set<physics::RigidBody>({});
    box.set<scripting::Script>({.script = *script});
    fixture.frame(editor);

    // Pausing outside play mode does nothing.
    editor.pause();
    REQUIRE_FALSE(editor.isPaused());

    editor.play();
    for (int frame = 0; frame < 12; ++frame) {
      fixture.frame(editor);
    }
    REQUIRE_FALSE(editor.isPaused());
    editor.pause();
    REQUIRE(editor.isPaused());
    REQUIRE(editor.isPlaying());
    REQUIRE(editor.audio().paused());

    // Nothing moves while paused: not the body, not the script's spin.
    const world::Transform frozen = world.find(boxUuid).get<world::Transform>();
    const float spun = world.find(boxUuid).get<world::Transform>().rotation.y;
    REQUIRE(frozen.position.y > 0.6f);
    for (int frame = 0; frame < 30; ++frame) {
      fixture.frame(editor);
    }
    REQUIRE(world.find(boxUuid).get<world::Transform>().position == frozen.position);
    REQUIRE(world.find(boxUuid).get<world::Transform>().rotation.y == spun);

    // An edit on the frozen body takes effect at once, and again when it resumes.
    world.find(boxUuid).set<world::Transform>({.position = {5.0f, frozen.position.y, 0.0f}});
    fixture.frame(editor);
    REQUIRE(world.find(boxUuid).get<world::WorldTransform>().matrix[3].x == 5.0f);
    REQUIRE(world.find(boxUuid).get<world::Transform>().position.y == frozen.position.y);

    editor.resume();
    REQUIRE_FALSE(editor.isPaused());
    REQUIRE_FALSE(editor.audio().paused());
    for (int frame = 0; frame < 6; ++frame) {
      fixture.frame(editor);
    }
    const world::Transform resumed = world.find(boxUuid).get<world::Transform>();
    REQUIRE(resumed.position.x == Catch::Approx(5.0f).margin(0.05f));
    REQUIRE(resumed.position.y != frozen.position.y);

    // Stop from a pause restores the scene as it was before play.
    editor.pause();
    editor.stop();
    REQUIRE_FALSE(editor.isPlaying());
    REQUIRE_FALSE(editor.isPaused());
    REQUIRE_FALSE(editor.audio().paused());
    const world::Transform restored = world.find(boxUuid).get<world::Transform>();
    REQUIRE(restored.position == glm::vec3{0.0f, 0.5f, 0.0f});
    REQUIRE(editor.physics().bodyCount() == 0);
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
    // The spawner's crates are its children, not new roots.
    const auto crates = [&] { return world.children(byName(world, "Spawner")).size(); };
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
    REQUIRE(world.roots().size() == entities);
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

TEST_CASE("the basic sample's start scene plays its animations and sounds", "[editor][gpu][samples]") {
  Fixture fixture;
  const std::optional<std::filesystem::path> manifest =
      editor::locateSource("apps/samples/basic/project.json", {}, fixture.platform.basePath());
  if (!manifest) {
    SKIP("the sample was not found above " << fixture.platform.basePath().string());
  }
  const std::filesystem::path directory = std::filesystem::temp_directory_path() / "sonnet_editor_tests" / "animated";
  std::filesystem::remove_all(directory);
  std::filesystem::create_directories(directory.parent_path());
  std::filesystem::copy(manifest->parent_path(), directory, std::filesystem::copy_options::recursive);
  const auto sink = std::make_shared<ProblemSink>();
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    core::Log::addSink(sink);
    REQUIRE(editor.openProject(directory).has_value()); // opens the start scene
    world::World &world = editor.world();
    const flecs::entity reed = byName(world, "Reed");
    const flecs::entity beacon = byName(world, "Beacon");
    const flecs::entity stem = world.findByPath(reed, "Reed/Stem");
    REQUIRE(stem);
    REQUIRE(stem.has<world::SkinnedMesh>());
    // Opening placed the model from its hierarchy alone; its first frame asks for the file, which
    // imports off the main thread, and the frame after the import publishes poses the skin.
    fixture.frame(editor);
    REQUIRE(editor.assets().loading());
    REQUIRE_FALSE(stem.has<world::SkinPose>());
    editor.assets().waitForLoads();
    fixture.frame(editor);
    // The skin poses its joints in edit mode; the clip waits for play.
    REQUIRE(stem.get<world::SkinPose>().joints.size() == 4);
    REQUIRE(reed.get<world::Animator>().time == 0.0f);
    REQUIRE(editor.audio().playingCount() == 0);

    editor.play();
    for (int frame = 0; frame < 60; ++frame) {
      fixture.frame(editor);
    }
    // The stem bends away from its bind pose, the lamp turns, and the beacon hums.
    REQUIRE(reed.get<world::Animator>().time > 0.0f);
    const glm::mat4 tip = stem.get<world::SkinPose>().joints.back();
    REQUIRE(std::abs(tip[3].x) > 0.01f);
    REQUIRE(world.findByPath(beacon, "Beacon").get<world::Transform>().rotation.y != 0.0f);
    REQUIRE(editor.audio().playingCount() == 1);
    REQUIRE(editor.audio().isPlaying(beacon));

    editor.stop();
    fixture.frame(editor);
    REQUIRE(editor.audio().playingCount() == 0);
    REQUIRE(byName(world, "Reed").get<world::Animator>().time == 0.0f);
    REQUIRE(sink->problems.empty());
    core::Log::removeSink(sink);
  }
  fixture.device->waitIdle();
  REQUIRE(fixture.device->validationMessageCount() == 0);
  std::filesystem::remove_all(directory);
}
