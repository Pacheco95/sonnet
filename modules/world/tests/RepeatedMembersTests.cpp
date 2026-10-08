#include <sonnet/world/Scene.h>
#include <sonnet/world/World.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace sonnet;
using Catch::Approx;

namespace {

// Shaped like the scripting module's Scripts, which world cannot see: a list of structs, one of
// whose strings holds JSON text.
struct Slot {
  core::Uuid script{};
  std::string properties{};
};

struct Slots {
  std::vector<Slot> slots{};
};

struct Waypoint {
  glm::vec3 position{0.0f};
  float wait{0.0f};
};

struct Route {
  std::vector<Waypoint> points{};
  std::vector<Slot> unused{};
};

void registerTypes(world::World &world) {
  world.ecs().component<Slot>("Slot").member<core::Uuid>("script").member<std::string>("properties");
  world.registerVector<Slot>();
  world.registerComponent<Slots>("Scripts").member<std::vector<Slot>>("slots");
  world.embedJson("Scripts", "properties");

  world.ecs().component<Waypoint>("Waypoint").member<glm::vec3>("position").member<float>("wait");
  world.registerVector<Waypoint>();
  world.registerVector<Slot>();
  world.registerComponent<Route>("Route").member<std::vector<Waypoint>>("points").member<std::vector<Slot>>("unused");
}

constexpr const char *FirstId = "1b6e0c1a-0001-4a5b-8c9d-000000000001";

} // namespace

TEST_CASE("a repeated member of structs serializes as a JSON array", "[world][reflection]") {
  world::World world;
  registerTypes(world);
  const flecs::entity entity = world.createEntity("walker");
  entity.set<Route>({.points = {{.position = {1.0f, 2.0f, 3.0f}, .wait = 0.5f}, {.position = {4.0f, 0.0f, 0.0f}}}});

  const nlohmann::json json = world.componentToJson(entity, world.findComponent("Route")->id);
  REQUIRE(json["points"].is_array());
  REQUIRE(json["points"].size() == 2);
  REQUIRE(json["points"][0]["position"]["y"] == Approx(2.0));
  REQUIRE(json["points"][0]["wait"] == Approx(0.5));
  REQUIRE(json["unused"].is_array());
  REQUIRE(json["unused"].empty());

  const flecs::entity copy = world.createEntity("copy");
  world.componentFromJson(copy, world.findComponent("Route")->id, json);
  const Route &loaded = copy.get<Route>();
  REQUIRE(loaded.points.size() == 2);
  REQUIRE(loaded.points[0].position.z == Approx(3.0f));
  REQUIRE(loaded.points[1].position.x == Approx(4.0f));
}

TEST_CASE("loading a shorter list replaces the longer one it finds", "[world][reflection]") {
  world::World world;
  registerTypes(world);
  const flecs::entity entity = world.createEntity("walker");
  entity.set<Route>({.points = {{}, {}, {}}});
  const flecs::entity_t route = world.findComponent("Route")->id;
  world.componentFromJson(entity, route, nlohmann::json::parse(R"({"points": [{"wait": 2.0}]})"));
  REQUIRE(entity.get<Route>().points.size() == 1);
  REQUIRE(entity.get<Route>().points[0].wait == Approx(2.0f));
}

TEST_CASE("the meta cursor walks and grows a repeated member", "[world][reflection]") {
  world::World world;
  registerTypes(world);
  const flecs::entity entity = world.createEntity("walker");
  entity.set<Route>({.points = {{.wait = 1.0f}, {.wait = 2.0f}}});
  const flecs::entity_t route = world.findComponent("Route")->id;
  void *data = entity.ensure(route);

  // What the inspector does: push into the struct, then into the collection.
  ecs_meta_cursor_t cursor = ecs_meta_cursor(world.ecs().c_ptr(), route, data);
  REQUIRE(ecs_meta_push(&cursor) == 0);
  REQUIRE(ecs_meta_member(&cursor, "points") == 0);
  REQUIRE(ecs_meta_push(&cursor) == 0);
  REQUIRE(ecs_meta_elem(&cursor, 2) == 0); // one past the end appends through ensure_element
  REQUIRE(ecs_meta_push(&cursor) == 0);
  REQUIRE(ecs_meta_member(&cursor, "wait") == 0);
  REQUIRE(ecs_meta_set_float(&cursor, 7.0) == 0);
  REQUIRE(entity.get<Route>().points.size() == 3);
  REQUIRE(entity.get<Route>().points[2].wait == Approx(7.0f));
  REQUIRE(entity.get<Route>().points[1].wait == Approx(2.0f));
}

TEST_CASE("a JSON string member is written as the JSON it holds", "[world][reflection]") {
  world::World world;
  registerTypes(world);
  const flecs::entity entity = world.createEntity("thing");
  entity.set<Slots>({.slots = {{.script = core::Uuid::parse(FirstId).value(), .properties = R"({"speed":3.5})"},
                               {.script = core::Uuid{}, .properties = ""}}});
  const flecs::entity_t id = world.findComponent("Scripts")->id;
  const nlohmann::json json = world.componentToJson(entity, id);
  REQUIRE(json["slots"][0]["properties"].is_object());
  REQUIRE(json["slots"][0]["properties"]["speed"] == Approx(3.5));
  REQUIRE(json["slots"][1]["properties"].is_object());
  REQUIRE(json["slots"][1]["properties"].empty());

  const flecs::entity copy = world.createEntity("copy");
  world.componentFromJson(copy, id, json);
  const Slots &loaded = copy.get<Slots>();
  REQUIRE(loaded.slots.size() == 2);
  REQUIRE(nlohmann::json::parse(loaded.slots[0].properties)["speed"] == Approx(3.5));
  REQUIRE(loaded.slots[1].properties.empty());
}

TEST_CASE("a scene with repeated members round-trips and keeps the slot order", "[world][scene][reflection]") {
  world::World source;
  registerTypes(source);
  const flecs::entity entity = source.createEntity("thing");
  entity.set<Slots>({.slots = {{.script = core::Uuid::parse(FirstId).value(), .properties = R"({"a":1})"},
                               {.script = core::Uuid::generate(), .properties = R"({"b":"x"})"}}});
  const nlohmann::json scene = world::saveScene(source);
  REQUIRE(scene["entities"][0]["components"]["Scripts"]["slots"].size() == 2);

  world::World target;
  registerTypes(target);
  REQUIRE(world::loadScene(target, scene));
  const flecs::entity found = target.find(source.uuidOf(entity));
  const Slots &loaded = found.get<Slots>();
  REQUIRE(loaded.slots.size() == 2);
  REQUIRE(loaded.slots[0].script.toString() == FirstId);
  REQUIRE(loaded.slots[1].script == entity.get<Slots>().slots[1].script);
  REQUIRE(nlohmann::json::parse(loaded.slots[1].properties)["b"] == "x");
}

TEST_CASE("a version 2 scene migrates each Script to a one-slot Scripts", "[world][scene][migration]") {
  const nlohmann::json old = nlohmann::json::parse(R"({
    "version": 2,
    "entities": [
      {"uuid": "1b6e0c1a-0001-4a5b-8c9d-000000000001", "name": "Scripted",
       "components": {"Script": {"script": "5a0c2f5e-0002-4a5b-8c9d-0000000000aa"}}},
      {"uuid": "1b6e0c1a-0001-4a5b-8c9d-000000000002", "name": "Plain", "components": {}}
    ]
  })");
  world::World world;
  registerTypes(world);
  REQUIRE(world::loadScene(world, old));
  const flecs::entity scripted = world.find(core::Uuid::parse(FirstId).value());
  const Slots &slots = scripted.get<Slots>();
  REQUIRE(slots.slots.size() == 1);
  REQUIRE(slots.slots[0].script.toString() == "5a0c2f5e-0002-4a5b-8c9d-0000000000aa");
  REQUIRE(slots.slots[0].properties.empty());

  const nlohmann::json saved = world::saveScene(world);
  REQUIRE(saved["version"] == 3);
  const nlohmann::json &components = saved["entities"][0]["components"];
  REQUIRE(!components.contains("Script"));
  REQUIRE(components["Scripts"]["slots"][0]["script"] == "5a0c2f5e-0002-4a5b-8c9d-0000000000aa");
}
