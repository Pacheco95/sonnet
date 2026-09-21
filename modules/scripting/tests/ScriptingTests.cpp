#include <sonnet/scripting/ScriptRuntime.h>

#include <sonnet/assets/AssetDatabase.h>
#include <sonnet/core/File.h>
#include <sonnet/core/JobSystem.h>
#include <sonnet/core/Log.h>
#include <sonnet/physics/PhysicsWorld.h>
#include <sonnet/platform/InputState.h>
#include <sonnet/platform/Platform.h>
#include <sonnet/renderer/Renderer.h>
#include <sonnet/rhi/NullDevice.h>
#include <sonnet/world/Scene.h>
#include <sonnet/world/World.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <spdlog/sinks/base_sink.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

using namespace sonnet;
using Catch::Approx;

namespace {

constexpr float Step = 1.0f / 60.0f;

struct Record {
  spdlog::level::level_enum level;
  std::string file;
  int line;
  std::string message;
};

// Keeps what the scripting logger writes, with the record's location.
class RecordingSink final : public spdlog::sinks::base_sink<std::mutex> {
public:
  std::vector<Record> records;

  [[nodiscard]] const Record *find(std::string_view text) const {
    const auto it = std::ranges::find_if(records, [&](const Record &record) { return record.message.contains(text); });
    return it != records.end() ? &*it : nullptr;
  }

protected:
  void sink_it_(const spdlog::details::log_msg &message) override {
    records.push_back({message.level, message.source.filename != nullptr ? message.source.filename : "",
                       message.source.line, std::string{message.payload.data(), message.payload.size()}});
  }
  void flush_() override {
  }
};

// A project with the given scripts under assets/, and everything a runtime runs on. Members are
// declared in the order they depend on each other, so they are destroyed in reverse.
struct Fixture {
  // Long enough that Lua shortens a script's path in its messages on every platform, as it does
  // under macOS's temporary directory.
  std::filesystem::path root =
      std::filesystem::temp_directory_path() / "sonnet_scripting_tests_under_a_directory_longer_than_lua_idsize";
  platform::Platform platform{{.headless = true}};
  std::unique_ptr<rhi::NullDevice> device = rhi::createNullDevice();
  renderer::Renderer renderer{*device, platform.basePath() / "shaders"};
  assets::AssetDatabase assets{renderer, jobs};
  world::World world;
  // Two workers rather than the machine's count: enough to run Jolt's jobs on the pool rather
  // than inline, few enough that a fixture per test case stays cheap.
  core::JobSystem jobs{{.workerCount = 2}};
  std::unique_ptr<physics::IPhysicsWorld> physics = physics::createPhysicsWorld(world, assets, jobs);
  platform::InputState input;
  std::unique_ptr<scripting::IScriptRuntime> scripts;
  std::shared_ptr<RecordingSink> sink = std::make_shared<RecordingSink>();

  explicit Fixture(std::initializer_list<std::pair<std::string_view, std::string_view>> files = {}) {
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "assets");
    for (const auto &[name, code] : files) {
      write(name, code);
    }
    const std::vector<std::string> roots{"assets"};
    assets.open(root, roots);
    scripts =
        scripting::createScriptRuntime({.world = &world, .assets = &assets, .physics = physics.get(), .input = &input});
    core::Log::addSink(sink);
  }
  ~Fixture() {
    core::Log::removeSink(sink);
    scripts.reset();
    physics.reset();
    world.clearScene();
    std::filesystem::remove_all(root);
  }
  Fixture(const Fixture &) = delete;
  Fixture &operator=(const Fixture &) = delete;

  void write(std::string_view name, std::string_view code) const {
    REQUIRE(core::writeFile(root / "assets" / name, std::as_bytes(std::span{code.data(), code.size()})).has_value());
  }

  [[nodiscard]] core::Uuid script(std::string_view name) const {
    const assets::AssetInfo *info = assets.findByPath(root / "assets" / name);
    REQUIRE(info != nullptr);
    return info->uuid;
  }

  flecs::entity scripted(std::string_view name, std::string_view file) {
    const flecs::entity entity = world.createEntity(name);
    entity.set<scripting::Script>({.script = script(file)});
    return entity;
  }

  void run(int frames) {
    for (int i = 0; i < frames; ++i) {
      world.progress(Step);
    }
  }
};

} // namespace

TEST_CASE("scripts get vec3 and quat maths and no file or OS access", "[scripting]") {
  Fixture fixture;
  REQUIRE(fixture.scripts
              ->run(R"lua(
    local a = vec3(1, 2, 3) + vec3(1, 1, 1)
    assert(a == vec3(2, 3, 4), tostring(a))
    assert((a * 2).x == 4 and (2 * a).y == 6 and (-a).z == -4)
    assert(vec3(3, 4, 0):length() == 5)
    assert(vec3(1, 0, 0):cross(vec3(0, 1, 0)) == vec3(0, 0, 1))
    local turned = quat.axisAngle(vec3(0, 1, 0), math.pi / 2) * vec3(1, 0, 0)
    assert(math.abs(turned.x) < 1e-6 and math.abs(turned.z + 1) < 1e-6, tostring(turned))
    local q = quat.axisAngle(vec3(0, 0, 1), 0.3)
    local back = q:inverse() * (q * vec3(1, 2, 3))
    assert((back - vec3(1, 2, 3)):length() < 1e-6)
    assert(io == nil and os == nil and dofile == nil and loadfile == nil and require == nil)
  )lua",
                    "maths")
              .has_value());

  const auto failed = fixture.scripts->run("local x = nil + 1", "broken");
  REQUIRE(!failed.has_value());
  REQUIRE(failed.error().category == core::ErrorCategory::Script);
  REQUIRE(failed.error().message.contains("broken:1:"));
  REQUIRE(!fixture.scripts->run("this is not lua", "syntax").has_value());
}

TEST_CASE("a script's instance starts once and updates every frame in play mode only", "[scripting]") {
  Fixture fixture{{"mover.lua", R"lua(
    local Mover = { speed = 2 }
    function Mover:start()
      self.starts = (self.starts or 0) + 1
      self.entity:set("Name", nil)
    end
    function Mover:update(dt)
      local transform = self.entity:get("Transform")
      transform.position = transform.position + vec3(self.speed * dt, 0, 0)
      self.entity:set("Transform", transform)
    end
    return Mover
  )lua"}};
  const flecs::entity mover = fixture.scripted("Mover", "mover.lua");
  fixture.run(3);
  REQUIRE(fixture.scripts->instanceCount() == 0);
  REQUIRE(mover.get<world::Transform>().position.x == 0.0f);

  fixture.world.setPlaying(true);
  fixture.run(1);
  // start raised an error ("Name" is not a registered component), so the instance is off.
  REQUIRE(fixture.scripts->instanceCount() == 1);
  REQUIRE(mover.get<world::Transform>().position.x == 0.0f);
  const Record *error = fixture.sink->find("there is no component named \"Name\"");
  REQUIRE(error != nullptr);
  REQUIRE(error->level == spdlog::level::err);
  REQUIRE(error->file.ends_with("mover.lua"));
  REQUIRE(error->line == 5);
  REQUIRE(fixture.sink->find("the script stops on \"Mover\"") != nullptr);

  // A fixed script, reloaded under the running instance, which keeps its state and is not
  // started again.
  fixture.write("mover.lua", R"lua(
    local Mover = { speed = 2 }
    function Mover:start() self.starts = (self.starts or 0) + 1 end
    function Mover:update(dt)
      local transform = self.entity:get("Transform")
      transform.position = transform.position + vec3(self.speed * dt, 0, 0)
      self.entity:set("Transform", transform)
      self.entity:set("Spin", { speed = self.starts })
    end
    return Mover
  )lua");
  REQUIRE(fixture.assets.reimport(fixture.script("mover.lua")).has_value());
  fixture.run(30);
  REQUIRE(mover.get<world::Transform>().position.x == Approx(1.0f).margin(1e-4f));
  REQUIRE(mover.get<world::Spin>().speed == 1.0f);
  REQUIRE(fixture.sink->find("reloaded mover.lua") != nullptr);

  fixture.world.setPlaying(false);
  fixture.run(10);
  REQUIRE(mover.get<world::Transform>().position.x == Approx(1.0f).margin(1e-4f));
}

TEST_CASE("components cross into Lua through reflection", "[scripting]") {
  Fixture fixture{{"probe.lua", R"lua(
    local Probe = {}
    function Probe:start()
      local e = self.entity
      local renderer = e:get("MeshRenderer")
      assert(renderer.mesh == "c0000000-0000-4000-8000-000000000001" or type(renderer.mesh) == "string")
      assert(renderer.material == nil)
      assert(renderer.visible == true)
      assert(e:get("RigidBody") == nil and not e:has("RigidBody"))
      e:add("RigidBody")
      local body = e:get("RigidBody")
      assert(body.type == "Dynamic", body.type)
      e:set("RigidBody", { type = "Kinematic", mass = 3 })
      assert(e:get("RigidBody").type == "Kinematic" and e:get("RigidBody").mass == 3)
      assert(e:get("Static") == nil)
      e:add("Static")
      assert(e:get("Static") == true and e:has("Static"))
      e:remove("Static")
      assert(not e:has("Static"))
      local rotation = e:get("Transform").rotation
      assert(rotation == quat.identity(), tostring(rotation))
      assert(e:name() == "Probe" and #e:uuid() == 36)
      local ok, message = pcall(function() e:set("RigidBody", { type = "Floating" }) end)
      assert(not ok and message:find("Floating") and message:find("Kinematic"), message)
      ok, message = pcall(function() e:set("Transform", { position = 3 }) end)
      assert(not ok and message:find("position: expected a table"), message)
      ok, message = pcall(function() e:set("MeshRenderer", { mesh = "nonsense" }) end)
      assert(not ok and message:find("not an asset identity"), message)
      self.passed = true
      e:set("Spin", { speed = 7 })
    end
    return Probe
  )lua"}};
  const flecs::entity probe = fixture.scripted("Probe", "probe.lua");
  probe.set<world::MeshRenderer>({});
  fixture.world.setPlaying(true);
  fixture.run(1);
  REQUIRE(fixture.sink->find("stops on") == nullptr);
  REQUIRE(probe.get<world::Spin>().speed == 7.0f);
  REQUIRE(probe.get<physics::RigidBody>().type == physics::BodyType::Kinematic);
}

TEST_CASE("scripts find, create, instantiate and destroy entities", "[scripting]") {
  Fixture fixture{{"spawner.lua", R"lua(
    local Spawner = {}
    function Spawner:start()
      local target = world.findByName("Target")
      assert(target and target:name() == "Target")
      assert(world.find(target:uuid()) == target)
      assert(world.findByName("Nobody") == nil)
      local child = world.create("Child", self.entity)
      assert(child:parent() == self.entity and #self.entity:children() == 1)
      child:set("Transform", { position = { x = 0, y = 2, z = 0 } })
      local p = child:worldPosition()
      assert(p == vec3(5, 2, 0), tostring(p))
      local crate = world.instantiate("Crate")
      assert(crate:name() == "Crate" and crate:has("BoxCollider"))
      world.instantiate("Crate", "Second crate", self.entity)
      local ok, message = pcall(world.instantiate, "Unknown")
      assert(not ok and message:find("no prefab"), message)
      target:destroy()
      assert(not target:isValid())
      ok, message = pcall(function() return target:name() end)
      assert(not ok and message:find("no longer exists"), message)
      self.entity:set("Spin", { speed = 1 })
    end
    return Spawner
  )lua"}};
  const flecs::entity prefab = fixture.world.createEntity("Crate");
  prefab.add(flecs::Prefab);
  prefab.set<physics::BoxCollider>({});
  const flecs::entity spawner = fixture.scripted("Spawner", "spawner.lua");
  spawner.set<world::Transform>({.position = {5.0f, 0.0f, 0.0f}});
  const flecs::entity target = fixture.world.createEntity("Target");
  fixture.world.setPlaying(true);
  fixture.run(1);
  REQUIRE(fixture.sink->find("stops on") == nullptr);
  REQUIRE(spawner.has<world::Spin>());
  REQUIRE(!target.is_alive());
  REQUIRE(fixture.world.children(spawner).size() == 2);
}

TEST_CASE("scripts read input and drive physics after its step", "[scripting]") {
  Fixture fixture{{"ball.lua", R"lua(
    local Ball = {}
    function Ball:fixedUpdate(dt)
      if input.keyDown("Space") then
        physics.addImpulse(self.entity, vec3(0, 5, 0))
      end
      local hit = physics.raycast(self.entity:worldPosition(), vec3(0, -1, 0), 100, self.entity)
      self.grounded = hit ~= nil and hit.entity:name() == "Ground" and hit.distance < 0.6
      self.speed = physics.linearVelocity(self.entity).y
    end
    function Ball:update(dt)
      local ok = pcall(input.keyDown, "NoSuchKey")
      assert(not ok)
      self.entity:set("Spin", { speed = self.grounded and 1 or 0 })
    end
    return Ball
  )lua"}};
  const flecs::entity ground = fixture.world.createEntity("Ground");
  ground.set<world::Transform>({.position = {0.0f, -0.5f, 0.0f}});
  ground.set<physics::BoxCollider>({.halfExtents = {10.0f, 0.5f, 10.0f}});
  const flecs::entity ball = fixture.scripted("Ball", "ball.lua");
  ball.set<world::Transform>({.position = {0.0f, 0.5f, 0.0f}});
  ball.set<physics::SphereCollider>({});
  ball.set<physics::RigidBody>({});
  fixture.world.setPlaying(true);
  fixture.run(30);
  REQUIRE(fixture.sink->find("stops on") == nullptr);
  REQUIRE(ball.get<world::Spin>().speed == 1.0f);

  fixture.input.beginFrame();
  fixture.input.handle(platform::KeyPressed{.key = platform::Key::Space, .modifiers = {}, .repeat = false});
  fixture.run(1);
  fixture.input.handle(platform::KeyReleased{.key = platform::Key::Space, .modifiers = {}});
  fixture.run(20);
  REQUIRE(ball.get<world::Transform>().position.y > 1.0f);
  REQUIRE(ball.get<world::Spin>().speed == 0.0f);
}

TEST_CASE("script log calls carry the script's file and line", "[scripting]") {
  Fixture fixture{{"talker.lua", "local Talker = {}\nfunction Talker:start()\n  log.warn(\"hello\", 42, true)\n  "
                                 "print(\"printed\")\nend\nreturn Talker\n"}};
  fixture.scripted("Talker", "talker.lua");
  fixture.world.setPlaying(true);
  fixture.run(1);
  const Record *warning = fixture.sink->find("hello 42 true");
  REQUIRE(warning != nullptr);
  REQUIRE(warning->level == spdlog::level::warn);
  REQUIRE(std::filesystem::path{warning->file} == fixture.root / "assets" / "talker.lua");
  REQUIRE(warning->line == 3);
  const Record *printed = fixture.sink->find("printed");
  REQUIRE(printed != nullptr);
  REQUIRE(printed->level == spdlog::level::info);
  REQUIRE(printed->line == 4);
}

TEST_CASE("broken and missing scripts are reported once and run nothing", "[scripting]") {
  Fixture fixture{{"syntax.lua", "local Broken = {\nreturn Broken\n"}, {"nothing.lua", "local x = 1\n"}};
  fixture.scripted("Syntax", "syntax.lua");
  fixture.scripted("Nothing", "nothing.lua");
  const flecs::entity missing = fixture.world.createEntity("Missing");
  missing.set<scripting::Script>({.script = core::Uuid::generate()});
  fixture.world.createEntity("Empty").set<scripting::Script>({});
  fixture.world.setPlaying(true);
  fixture.run(5);
  REQUIRE(fixture.scripts->instanceCount() == 0);
  const auto count = [&](std::string_view text) {
    return std::ranges::count_if(fixture.sink->records,
                                 [&](const Record &record) { return record.message.contains(text); });
  };
  REQUIRE(count("has to return its table") == 1);
  REQUIRE(count("is not a script asset") == 1);
  const Record *syntax = fixture.sink->find("expected");
  REQUIRE(syntax != nullptr);
  REQUIRE(syntax->file.ends_with("syntax.lua"));
}

TEST_CASE("reset drops instances and script state, and entities going away drop theirs", "[scripting]") {
  Fixture fixture{{"counter.lua", R"lua(
    local Counter = {}
    function Counter:start()
      count = (count or 0) + 1
      self.entity:set("Spin", { speed = count })
    end
    return Counter
  )lua"}};
  const flecs::entity first = fixture.scripted("First", "counter.lua");
  const flecs::entity second = fixture.scripted("Second", "counter.lua");
  const nlohmann::json snapshot = world::saveScene(fixture.world);
  fixture.world.setPlaying(true);
  fixture.run(1);
  REQUIRE(fixture.scripts->instanceCount() == 2);
  // Instances of one script share its globals.
  REQUIRE(first.get<world::Spin>().speed == 1.0f);
  REQUIRE(second.get<world::Spin>().speed == 2.0f);

  second.add<world::Disabled>();
  fixture.run(1);
  REQUIRE(fixture.scripts->instanceCount() == 1);
  fixture.world.destroyEntity(first);
  fixture.run(1);
  REQUIRE(fixture.scripts->instanceCount() == 0);

  // What the editor does on stop; the script's globals start over.
  fixture.world.setPlaying(false);
  fixture.world.clearScene();
  fixture.scripts->reset();
  REQUIRE(world::loadScene(fixture.world, snapshot).has_value());
  fixture.world.setPlaying(true);
  fixture.run(1);
  REQUIRE(fixture.scripts->instanceCount() == 2);
  std::vector<float> speeds;
  for (const flecs::entity root : fixture.world.roots()) {
    speeds.push_back(root.get<world::Spin>().speed);
  }
  std::ranges::sort(speeds);
  REQUIRE(speeds == std::vector<float>{1.0f, 2.0f});
}
