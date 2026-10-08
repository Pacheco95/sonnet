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
#include <sonnet/world/Animation.h>
#include <sonnet/world/Scene.h>
#include <sonnet/world/World.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <spdlog/sinks/base_sink.h>

#include <algorithm>
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
  scripting::ScriptView view;
  world::AnimationSystem animation{world, assets};
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
    scripts = scripting::createScriptRuntime({.world = &world,
                                              .assets = &assets,
                                              .physics = physics.get(),
                                              .animation = &animation,
                                              .input = &input,
                                              .view = &view});
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
    entity.set<scripting::Scripts>({.slots = {{.script = script(file)}}});
    return entity;
  }

  void run(int frames) {
    for (int i = 0; i < frames; ++i) {
      world.progress(Step);
    }
  }

  // What scripts printed with a leading "@": a trace to compare in order, without the mark.
  [[nodiscard]] std::vector<std::string> trace() const {
    std::vector<std::string> lines;
    for (const Record &record : sink->records) {
      if (record.message.starts_with("@")) {
        lines.push_back(record.message.substr(1));
      }
    }
    return lines;
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
    assert(io == nil and os == nil and dofile == nil and loadfile == nil and package == nil)
    assert(type(require) == "function")
  )lua",
                    "maths")
              .has_value());

  const auto failed = fixture.scripts->run("local x = nil + 1", "broken");
  REQUIRE(!failed.has_value());
  REQUIRE(failed.error().category == core::ErrorCategory::Script);
  REQUIRE(failed.error().message.contains("broken:1:"));
  REQUIRE(!fixture.scripts->run("this is not lua", "syntax").has_value());
}

TEST_CASE("a seeded math.random repeats its sequence", "[scripting]") {
  Fixture fixture;
  // Globals through _G, since every run gets an environment of its own over them.
  const auto draw = [&](std::string_view into) {
    REQUIRE(fixture.scripts
                ->run(std::format("_G.{} = {{ math.random(), math.random(), math.random(1, 1000000) }}", into), "draw")
                .has_value());
  };
  fixture.scripts->seedRandom(1);
  draw("first");
  fixture.scripts->seedRandom(1);
  draw("again");
  fixture.scripts->seedRandom(2);
  draw("other");
  REQUIRE(fixture.scripts
              ->run(R"lua(
    for i = 1, 3 do
      assert(first[i] == again[i], "the same seed gave a different draw " .. i)
    end
    assert(first[1] ~= other[1] or first[2] ~= other[2] or first[3] ~= other[3], "another seed gave the same draws")
  )lua",
                    "compare")
              .has_value());
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

TEST_CASE("scripts read the touches with their positions and this frame's motion", "[scripting]") {
  Fixture fixture;
  REQUIRE(fixture.scripts->run("assert(#input.touches() == 0)", "=none").has_value());
  fixture.input.beginFrame();
  fixture.input.handle(platform::TouchDown{.id = 3, .position = {10.0f, 20.0f}});
  fixture.input.handle(platform::TouchDown{.id = 8, .position = {300.0f, 40.0f}});
  fixture.input.handle(platform::TouchMotion{.id = 3, .position = {12.5f, 19.0f}, .delta = {2.5f, -1.0f}});
  REQUIRE(fixture.scripts
              ->run(R"lua(
    local touches = input.touches()
    assert(#touches == 2, #touches)
    local first, second = touches[1], touches[2]
    assert(math.type(first.id) == "integer" and first.id == 3, tostring(first.id))
    assert(first.position.x == 12.5 and first.position.y == 19, first.position.x)
    assert(first.delta.x == 2.5 and first.delta.y == -1, first.delta.x)
    assert(second.id == 8 and second.position.x == 300 and second.delta.x == 0)
    -- The same shape as the mouse's.
    local mouse = input.mousePosition()
    assert(type(mouse.x) == type(first.position.x))
  )lua",
                    "=touches")
              .has_value());
  fixture.input.beginFrame();
  fixture.input.handle(platform::TouchUp{.id = 3, .position = {12.5f, 19.0f}});
  REQUIRE(fixture.scripts
              ->run(R"lua(
    local touches = input.touches()
    assert(#touches == 1 and touches[1].id == 8 and touches[1].delta.x == 0)
  )lua",
                    "=lifted")
              .has_value());
}

TEST_CASE("the camera turns a point of the view into a ray", "[scripting]") {
  Fixture fixture;
  // A 90-degree camera over a 200x100 view: its corners are one unit up and two across at a
  // distance of one.
  fixture.view.camera.position = {1.0f, 2.0f, 3.0f};
  fixture.view.camera.fovY = glm::radians(90.0f);
  fixture.view.size = {200.0f, 100.0f};
  REQUIRE(fixture.scripts
              ->run(R"lua(
    local centre = camera.ray({ x = 100, y = 50 })
    assert(centre.origin == vec3(1, 2, 3), tostring(centre.origin))
    assert((centre.direction - vec3(0, 0, -1)):length() < 1e-6, tostring(centre.direction))
    local corner = camera.ray({ x = 0, y = 0 }).direction
    assert((corner - vec3(-2, 1, -1):normalized()):length() < 1e-6, tostring(corner))
    -- What a script does with it: the point on the ground under a touch.
    local ray = camera.ray(input.mousePosition())
    assert(getmetatable(ray.direction) == getmetatable(vec3(0, 0, 0)))
    assert(not pcall(camera.ray, 5))
  )lua",
                    "=ray")
              .has_value());
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
  missing.set<scripting::Scripts>({.slots = {{.script = core::Uuid::generate()}}});
  fixture.world.createEntity("Empty").set<scripting::Scripts>({});
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

TEST_CASE("Scripts round-trips through a scene and a version 2 Script migrates into it", "[scripting][scene]") {
  Fixture fixture{{"spin.lua", "return {}"}};
  const core::Uuid spin = fixture.script("spin.lua");
  const flecs::entity entity = fixture.world.createEntity("Thing");
  entity.set<scripting::Scripts>(
      {.slots = {{.script = spin, .properties = R"({"speed":3})"}, {.script = spin, .properties = ""}}});
  const nlohmann::json scene = world::saveScene(fixture.world);
  const nlohmann::json &saved = scene["entities"][0]["components"]["Scripts"]["slots"];
  REQUIRE(saved.size() == 2);
  REQUIRE(saved[0]["properties"]["speed"] == 3);
  fixture.world.destroyEntity(entity);
  REQUIRE(world::loadScene(fixture.world, scene));
  const flecs::entity loaded = fixture.world.roots().front();
  REQUIRE(loaded.get<scripting::Scripts>().slots.size() == 2);
  REQUIRE(loaded.get<scripting::Scripts>().slots[0].script == spin);

  fixture.world.destroyEntity(loaded);
  const nlohmann::json old{{"version", 2},
                           {"entities",
                            {{{"uuid", core::Uuid::generate().toString()},
                              {"name", "Old"},
                              {"components", {{"Script", {{"script", spin.toString()}}}}}}}}};
  REQUIRE(world::loadScene(fixture.world, old));
  const flecs::entity migrated = fixture.world.roots().front();
  REQUIRE(migrated.get<scripting::Scripts>().slots.size() == 1);
  REQUIRE(migrated.get<scripting::Scripts>().slots[0].script == spin);
}

TEST_CASE("an entity runs several scripts, in slot order, each with its own instance", "[scripting][slots]") {
  Fixture fixture{{"a.lua", R"lua(
    local A = {}
    function A:start() print("@start a" .. self.slot) end
    function A:update() print("@update a" .. self.slot) end
    return A
  )lua"},
                  {"b.lua", R"lua(
    local B = {}
    function B:start() print("@start b" .. self.slot) end
    function B:update() print("@update b" .. self.slot) end
    return B
  )lua"}};
  const flecs::entity entity = fixture.world.createEntity("Multi");
  entity.set<scripting::Scripts>({.slots = {{.script = fixture.script("a.lua")},
                                            {.script = fixture.script("b.lua")},
                                            {.script = fixture.script("a.lua")}}});
  fixture.world.setPlaying(true);
  fixture.run(2);
  REQUIRE(fixture.scripts->instanceCount() == 3);
  const std::vector<std::string> expected{"start a1",  "start b2",  "start a3",  "update a1", "update b2",
                                          "update a3", "update a1", "update b2", "update a3"};
  REQUIRE(fixture.trace() == expected);

  // Taking a slot away ends its instance and leaves the others running; a nil slot runs nothing.
  entity.set<scripting::Scripts>({.slots = {{.script = fixture.script("a.lua")}, {.script = core::Uuid{}}}});
  fixture.sink->records.clear();
  fixture.run(1);
  REQUIRE(fixture.scripts->instanceCount() == 1);
  REQUIRE(fixture.trace() == std::vector<std::string>{"update a1"});
  // Another script in a slot starts fresh.
  entity.set<scripting::Scripts>({.slots = {{.script = fixture.script("b.lua")}}});
  fixture.sink->records.clear();
  fixture.run(1);
  REQUIRE(fixture.trace() == (std::vector<std::string>{"start b1", "update b1"}));
}

namespace {

constexpr const char *Listener = R"lua(
  local Listener = {}
  -- Adding zero turns -0.0 into 0.0, which some platforms print with its sign.
  local function fmt(v) return string.format("%.1f,%.1f,%.1f", v.x + 0, v.y + 0, v.z + 0) end
  function Listener:onContactBegin(other, contact)
    self.began = true
    print("@begin " .. self.entity:name() .. " " .. other:name() .. " " .. fmt(contact.normal))
    if self.onBegin then self.onBegin(self, other) end
  end
  function Listener:onContactEnd(other) print("@end " .. self.entity:name() .. " " .. other:name()) end
  function Listener:onTriggerEnter(other) print("@enter " .. self.entity:name() .. " " .. other:name()) end
  function Listener:onTriggerExit(other) print("@exit " .. self.entity:name() .. " " .. other:name()) end
  function Listener:fixedUpdate() print("@fixed " .. self.entity:name() .. " " .. tostring(self.began)) end
  return Listener
)lua";

bool before(const std::vector<std::string> &lines, std::string_view first, std::string_view second) {
  const auto a = std::ranges::find_if(lines, [&](const std::string &line) { return line.starts_with(first); });
  const auto b = std::ranges::find_if(lines, [&](const std::string &line) { return line.starts_with(second); });
  return a != lines.end() && b != lines.end() && a < b;
}

} // namespace

TEST_CASE("contact and trigger events reach the scripts of both entities before fixedUpdate", "[scripting][events]") {
  Fixture fixture{{"listener.lua", Listener}};
  const flecs::entity ball = fixture.world.createEntity("Ball");
  ball.set<world::Transform>({.position = {0.0f, 1.0f, 0.0f}});
  ball.set<physics::SphereCollider>({.radius = 0.5f});
  ball.set<physics::RigidBody>({.gravityScale = 0.0f});
  ball.set<scripting::Scripts>({.slots = {{.script = fixture.script("listener.lua")}}});
  const flecs::entity ground = fixture.world.createEntity("Ground");
  ground.set<world::Transform>({.position = {0.0f, -0.5f, 0.0f}});
  ground.set<physics::BoxCollider>({.halfExtents = {10.0f, 0.5f, 10.0f}});
  ground.set<scripting::Scripts>({.slots = {{.script = fixture.script("listener.lua")}}});
  const flecs::entity zone = fixture.world.createEntity("Zone");
  zone.set<world::Transform>({.position = {4.0f, 1.0f, 0.0f}});
  zone.set<physics::BoxCollider>({.halfExtents = {1.0f, 1.0f, 1.0f}});
  zone.add<physics::Trigger>();
  zone.set<scripting::Scripts>({.slots = {{.script = fixture.script("listener.lua")}}});
  fixture.world.setPlaying(true);
  fixture.physics->setLinearVelocity(ball, {0.0f, -2.0f, 0.0f});
  fixture.run(60);

  // Landing: each side hears the other, with the normal pointing from itself to the other.
  std::vector<std::string> trace = fixture.trace();
  REQUIRE(std::ranges::count_if(trace, [](const std::string &line) { return line.starts_with("begin"); }) == 2);
  REQUIRE(std::ranges::count(trace, "begin Ball Ground 0.0,-1.0,0.0") == 1);
  REQUIRE(std::ranges::count(trace, "begin Ground Ball 0.0,1.0,0.0") == 1);
  // The events of a step come before that step's hooks: the first hooks after the begins already
  // know about them, and the ones before did not.
  const auto lastBegin =
      std::ranges::find_if(trace.rbegin(), trace.rend(), [](const std::string &l) { return l.starts_with("begin"); });
  const std::size_t after = static_cast<std::size_t>(trace.rend() - lastBegin); // the line after it
  REQUIRE(after + 1 < trace.size());
  REQUIRE(trace[after] == "fixed Ball true");
  REQUIRE(trace[after + 1] == "fixed Ground true");
  REQUIRE(std::ranges::count(trace, "fixed Ball nil") >= 1);

  // Rolling through the zone: enter and exit, to the trigger and to the ball.
  fixture.sink->records.clear();
  fixture.physics->setLinearVelocity(ball, {6.0f, 0.0f, 0.0f});
  fixture.run(90);
  trace = fixture.trace();
  REQUIRE(std::ranges::count(trace, "enter Ball Zone") == 1);
  REQUIRE(std::ranges::count(trace, "enter Zone Ball") == 1);
  REQUIRE(std::ranges::count(trace, "exit Ball Zone") == 1);
  REQUIRE(std::ranges::count(trace, "exit Zone Ball") == 1);
  REQUIRE(before(trace, "enter", "exit"));
}

TEST_CASE("an entity destroyed or disabled since the step gets no event", "[scripting][events]") {
  const auto run = [](std::string_view onBegin) {
    Fixture fixture{{"listener.lua", Listener},
                    {"hostile.lua", std::format(R"lua(
      local Hostile = {{}}
      function Hostile:onContactBegin(other, contact)
        print("@hostile " .. other:name())
        {}
      end
      return Hostile
    )lua",
                                                onBegin)}};
    // The hostile ball has the lower id, so it hears first and does something to the ground.
    const flecs::entity ball = fixture.world.createEntity("Ball");
    ball.set<world::Transform>({.position = {0.0f, 0.4f, 0.0f}});
    ball.set<physics::SphereCollider>({.radius = 0.5f});
    ball.set<physics::RigidBody>({});
    ball.set<scripting::Scripts>({.slots = {{.script = fixture.script("hostile.lua")}}});
    const flecs::entity ground = fixture.world.createEntity("Ground");
    ground.set<world::Transform>({.position = {0.0f, -0.5f, 0.0f}});
    ground.set<physics::BoxCollider>({.halfExtents = {10.0f, 0.5f, 10.0f}});
    ground.set<scripting::Scripts>({.slots = {{.script = fixture.script("listener.lua")}}});
    fixture.world.setPlaying(true);
    fixture.run(10);
    return fixture.trace();
  };
  for (const std::string_view action : {"other:destroy()", "other:add('Disabled')"}) {
    const std::vector<std::string> trace = run(action);
    REQUIRE(std::ranges::count(trace, "hostile Ground") == 1);
    REQUIRE(std::ranges::count_if(trace, [](const std::string &l) { return l.starts_with("begin Ground"); }) == 0);
  }
}

TEST_CASE("a class declares properties and a slot's values overlay the defaults", "[scripting][properties]") {
  Fixture fixture{{"door.lua", R"lua(
    local Door = {
      speed = 1,
      properties = {
        speed2 = { type = "number", default = 2, min = 0, max = 10 },
        count = { type = "integer", default = 3 },
        open = { type = "boolean" },
        label = { type = "string", default = "hi" },
        offset = { type = "vec3", default = vec3(1, 2, 3) },
        tint = { type = "color", default = { r = 0.5, g = 0.25, b = 0.125 } },
        target = { type = "entity" },
        sound = { type = "asset" },
      },
    }
    function Door:start()
      print(string.format("@%g %d %s %s %g,%g,%g %g,%g,%g,%g %s %s %g", self.speed2, self.count, tostring(self.open),
                          self.label, self.offset.x, self.offset.y, self.offset.z, self.tint.r, self.tint.g,
                          self.tint.b, self.tint.a, self.target and self.target:name() or "none",
                          tostring(self.sound), self.speed))
      self.count = 99
    end
    function Door:update() print("@update " .. self.speed2 .. " " .. self.count) end
    return Door
  )lua"}};
  const core::Uuid door = fixture.script("door.lua");

  // What the inspector reads: sorted by name, with types, defaults and limits.
  const auto declared = fixture.scripts->properties(door);
  REQUIRE(declared.has_value());
  REQUIRE(declared->size() == 8);
  REQUIRE((*declared)[0].name == "count");
  REQUIRE((*declared)[0].type == scripting::PropertyType::Integer);
  REQUIRE((*declared)[0].defaultValue == 3);
  // count, label, offset, open, sound, speed2, target, tint
  const scripting::PropertyDecl &speed = declared->at(5);
  REQUIRE(speed.name == "speed2");
  REQUIRE(speed.min == 0.0);
  REQUIRE(speed.max == 10.0);
  REQUIRE(speed.defaultValue == 2.0);
  REQUIRE(declared->at(2).name == "offset");
  REQUIRE(declared->at(2).defaultValue == nlohmann::json({{"x", 1.0}, {"y", 2.0}, {"z", 3.0}}));
  REQUIRE(declared->at(7).name == "tint");
  REQUIRE(declared->at(7).defaultValue == nlohmann::json({{"r", 0.5}, {"g", 0.25}, {"b", 0.125}, {"a", 1.0}}));
  REQUIRE(declared->at(6).defaultValue.is_null());
  REQUIRE(!fixture.scripts->properties(core::Uuid::generate()).has_value());

  // A slot with nothing changed runs on the defaults.
  const flecs::entity plain = fixture.world.createEntity("Plain");
  plain.set<scripting::Scripts>({.slots = {{.script = door}}});
  // Another with values, a wrong one, and a name the class does not declare.
  const flecs::entity target = fixture.world.createEntity("Target");
  const core::Uuid sound = core::Uuid::generate();
  const flecs::entity custom = fixture.world.createEntity("Custom");
  custom.set<scripting::Scripts>(
      {.slots = {
           {.script = door,
            .properties = std::format(
                R"({{"speed2":7.5,"label":"bye","open":true,"offset":{{"x":4,"y":5,"z":6}},"target":"{}","sound":"{}","count":"oops","gone":1}})",
                fixture.world.uuidOf(target).toString(), sound.toString())}}});
  fixture.world.setPlaying(true);
  fixture.run(1);
  const std::vector<std::string> trace = fixture.trace();
  REQUIRE(trace.size() == 4);
  REQUIRE(std::ranges::count(trace, "2 3 false hi 1,2,3 0.5,0.25,0.125,1 none nil 1") == 1);
  REQUIRE(std::ranges::count(trace,
                             std::format("7.5 3 true bye 4,5,6 0.5,0.25,0.125,1 Target {} 1", sound.toString())) == 1);
  // The class's plain fields stay defaults every instance shares; the wrong value fell back, and
  // the undeclared name is reported but kept.
  const Record *wrong = fixture.sink->find("property \"count\" does not hold a valid integer");
  REQUIRE(wrong != nullptr);
  REQUIRE(wrong->level == spdlog::level::warn);
  REQUIRE(fixture.sink->find("property \"gone\" is not declared") != nullptr);
  REQUIRE(custom.get<scripting::Scripts>().slots[0].properties.contains("\"gone\":1"));

  // Edited while running: the changed property is set again, what the script did to the rest stays.
  fixture.sink->records.clear();
  custom.set<scripting::Scripts>({.slots = {{.script = door, .properties = R"({"speed2":9,"count":"oops"})"}}});
  fixture.run(1);
  REQUIRE(std::ranges::count(fixture.trace(), "update 9.0 99") == 1);
  // Back to the defaults by removing the values: the wrong one that fell back is the default
  // again, and the script's own change to the field is overwritten with it.
  custom.set<scripting::Scripts>({.slots = {{.script = door}}});
  fixture.sink->records.clear();
  fixture.run(1);
  REQUIRE(std::ranges::count(fixture.trace(), "update 2.0 99") == 1); // the plain entity
  REQUIRE(std::ranges::count(fixture.trace(), "update 2.0 3") == 1);
}

TEST_CASE("a malformed properties declaration is reported and skipped", "[scripting][properties]") {
  Fixture fixture{{"bad.lua", R"lua(
    return { properties = { good = { type = "number" }, nameless = {}, weird = { type = "matrix" },
                            clash = { type = "integer", default = 1.5 } } }
  )lua"}};
  const auto declared = fixture.scripts->properties(fixture.script("bad.lua"));
  REQUIRE(declared.has_value());
  REQUIRE(declared->size() == 1);
  REQUIRE(declared->front().name == "good");
  REQUIRE(fixture.sink->find("property \"nameless\" needs a type") != nullptr);
  REQUIRE(fixture.sink->find("property \"weird\" needs a type") != nullptr);
  REQUIRE(fixture.sink->find("the default of property \"clash\" is not a integer") != nullptr);
}

TEST_CASE("require loads a script file once, shares it and reloads its users when it changes", "[scripting][require]") {
  Fixture fixture{{"lib/util.lua", R"lua(
    print("@util loaded")
    return { twice = function(x) return x * 2 end, tag = "one" }
  )lua"},
                  {"a.lua", R"lua(
    local util = require("util")
    local A = {}
    function A:update() print("@a " .. util.twice(21) .. " " .. util.tag) end
    return A
  )lua"},
                  {"b.lua", R"lua(
    local util = require("lib/util")
    local again = require("lib/util.lua")
    assert(util == again)
    local B = {}
    function B:update() print("@b " .. util.tag) end
    return B
  )lua"}};
  fixture.scripted("A", "a.lua");
  fixture.scripted("B", "b.lua");
  fixture.world.setPlaying(true);
  fixture.run(1);
  // Loaded once for both scripts.
  REQUIRE(std::ranges::count(fixture.trace(), "util loaded") == 1);
  REQUIRE(std::ranges::count(fixture.trace(), "a 42 one") == 1);
  REQUIRE(std::ranges::count(fixture.trace(), "b one") == 1);

  // Changing the module reloads both scripts, which then see the new value.
  fixture.write("lib/util.lua", R"lua(
    print("@util loaded")
    return { twice = function(x) return x * 3 end, tag = "two" }
  )lua");
  REQUIRE(fixture.assets.reimport(fixture.script("lib/util.lua")).has_value());
  fixture.sink->records.clear();
  fixture.run(2);
  REQUIRE(std::ranges::count(fixture.trace(), "util loaded") == 1);
  REQUIRE(std::ranges::count(fixture.trace(), "a 63 two") == 2);
  REQUIRE(std::ranges::count(fixture.trace(), "b two") == 2);
  REQUIRE(fixture.sink->find("reloaded a.lua") != nullptr);
  REQUIRE(fixture.sink->find("reloaded b.lua") != nullptr);
}

TEST_CASE("require reports cycles, unknown and ambiguous names, and a module that fails", "[scripting][require]") {
  Fixture fixture{{"x.lua", "require('y') return {}"},
                  {"y.lua", "require('x') return {}"},
                  {"missing.lua", "require('nothing_here') return {}"},
                  {"one/dup.lua", "return 1"},
                  {"two/dup.lua", "return 2"},
                  {"ambiguous.lua", "require('dup') return {}"},
                  {"broken.lua", "error('module exploded')"},
                  {"user.lua", "require('broken') return {}"},
                  {"unlisted.lua", "assert(require('one/dup') == 1 and require('two/dup') == 2) return {}"}};
  fixture.scripted("X", "x.lua");
  fixture.scripted("Missing", "missing.lua");
  fixture.scripted("Ambiguous", "ambiguous.lua");
  fixture.scripted("User", "user.lua");
  fixture.scripted("Unlisted", "unlisted.lua");
  fixture.world.setPlaying(true);
  fixture.run(1);
  const Record *cycle = fixture.sink->find("require cycle: ");
  REQUIRE(cycle != nullptr);
  REQUIRE(cycle->message.contains("assets/x.lua -> assets/y.lua -> assets/x.lua"));
  REQUIRE(fixture.sink->find("there is no script \"nothing_here\"") != nullptr);
  REQUIRE(fixture.sink->find("\"dup\" is ambiguous") != nullptr);
  REQUIRE(fixture.sink->find("module exploded") != nullptr);
  // Paths tell the two apart, and a module that returns a number is returned as it is.
  REQUIRE(fixture.sink->find("unlisted.lua") == nullptr);
  REQUIRE(fixture.scripts->instanceCount() == 1);
}

namespace {

// One node, "Box", and a clip "Beat" that slides it a metre in a second, with an event named
// "beat" at half a second in its extras (the buffer is embedded).
constexpr const char *BeatModel =
    R"json({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"name":"Box"}],"animations":[{"name":"Beat","extras":{"events":[{"time":0.5,"name":"beat","argument":"half"}]},"samplers":[{"input":0,"output":1}],"channels":[{"sampler":0,"target":{"node":0,"path":"translation"}}]}],"buffers":[{"uri":"data:application/octet-stream;base64,AAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAA=","byteLength":32}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":8},{"buffer":0,"byteOffset":8,"byteLength":24}],"accessors":[{"bufferView":0,"componentType":5126,"count":2,"type":"SCALAR","min":[0],"max":[1]},{"bufferView":1,"componentType":5126,"count":2,"type":"VEC3"}]})json";

constexpr const char *BeatListener = R"lua(
  local Beat = {}
  function Beat:onAnimationEvent(name, argument) print("@event " .. self.entity:name() .. " " .. name .. " " .. argument) end
  return Beat
)lua";

} // namespace

TEST_CASE("an animation event reaches the scripts of its entity as onAnimationEvent", "[scripting][events]") {
  Fixture fixture{{"beat.gltf", BeatModel}, {"beat.lua", BeatListener}};
  const auto models = fixture.assets.assets(assets::AssetType::Model);
  REQUIRE(models.size() == 1);
  const assets::Model *model = fixture.assets.model(models[0]->uuid);
  REQUIRE(model != nullptr);
  const flecs::entity prefab = world::loadModelPrefab(fixture.world, *model, models[0]->uuid, "Beat");
  static_cast<void>(fixture.assets.requestAnimation(prefab.get<world::Animator>().clip));
  fixture.assets.waitForLoads();
  const flecs::entity instance = fixture.world.instantiate(prefab, "Drum");
  instance.set<scripting::Scripts>({.slots = {{.script = fixture.script("beat.lua")}}});

  fixture.world.setPlaying(true);
  fixture.run(20); // a third of a second: before the event
  REQUIRE(std::ranges::none_of(fixture.trace(), [](const std::string &line) { return line.starts_with("event"); }));
  fixture.run(20); // across half a second
  std::vector<std::string> trace = fixture.trace();
  REQUIRE(std::ranges::count(trace, "event Drum beat half") == 1);
  // A second lap crosses it again.
  fixture.run(60);
  REQUIRE(std::ranges::count(fixture.trace(), "event Drum beat half") == 2); // the next lap
}
