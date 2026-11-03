// Per-field overrides on prefab instances (ADR-0037): a scene stores only the members an instance
// changed, merges them onto the prefab's current value on load, and pairs script slots by their
// script.
#include <sonnet/world/Scene.h>
#include <sonnet/world/World.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace sonnet;
using Catch::Approx;
using nlohmann::json;

namespace {

// Shaped like the scripting module's Scripts, which world cannot see, and registered the way
// scripting registers it.
struct Slot {
  core::Uuid script{};
  std::string properties{};
};

struct Slots {
  std::vector<Slot> slots{};
};

void registerSlots(world::World &world) {
  world.ecs().component<Slot>("Slot").member<core::Uuid>("script").member<std::string>("properties");
  world.registerVector<Slot>();
  world.registerComponent<Slots>("Scripts").member<std::vector<Slot>>("slots");
  world.embedJson("Scripts", "properties");
  world.matchBy("Scripts", "slots", "script");
}

const core::Uuid Mover = core::Uuid::parse("5a0c2f5e-0002-4a5b-8c9d-0000000000a1").value();
const core::Uuid Shooter = core::Uuid::parse("5a0c2f5e-0002-4a5b-8c9d-0000000000a2").value();
const core::Uuid Blinker = core::Uuid::parse("5a0c2f5e-0002-4a5b-8c9d-0000000000a3").value();

// A turret prefab: a root running some scripts and holding a light, with a "Barrel" child that
// runs the same scripts. Written as its file, so a test can change the prefab and load again.
struct Turret {
  core::Uuid root = core::Uuid::generate();
  core::Uuid barrel = core::Uuid::generate();
  json slots = json::array({{{"script", Mover.toString()}, {"properties", {{"speed", 2}, {"range", 5}}}},
                            {{"script", Shooter.toString()}, {"properties", json::object()}}});
  float lightRange = 10.0f;

  [[nodiscard]] json file() const {
    return {{"version", world::SceneVersion},
            {"entities",
             {{{"uuid", root.toString()},
               {"name", "Turret"},
               {"components",
                {{"Scripts", {{"slots", slots}}}, {"PointLight", {{"intensity", 1.0}, {"range", lightRange}}}}}},
              {{"uuid", barrel.toString()},
               {"name", "Barrel"},
               {"parent", root.toString()},
               {"components", {{"Scripts", {{"slots", slots}}}}}}}}};
  }

  void load(world::World &world) const {
    registerSlots(world);
    REQUIRE(world::loadPrefab(world, file()).has_value());
  }
};

json properties(const Slot &slot) {
  return slot.properties.empty() ? json::object() : json::parse(slot.properties);
}

flecs::entity barrelOf(const world::World &world, flecs::entity instance) {
  return world.children(instance).front();
}

// Sets one slot's properties on an entity, as the inspector does, overriding an inherited list.
void setProperties(flecs::entity entity, std::size_t slot, const json &values) {
  entity.ensure<Slots>().slots[slot].properties = values.empty() ? std::string{} : values.dump();
  entity.modified<Slots>();
}

const json &componentsOf(const json &scene, const core::Uuid &uuid) {
  for (const json &entry : scene["entities"]) {
    if (entry["uuid"] == uuid.toString()) {
      return entry["components"];
    }
  }
  FAIL("no entry for " << uuid.toString());
  static const json none;
  return none;
}

} // namespace

TEST_CASE("an instance that changes one script property keeps following the prefab for the rest",
          "[world][prefab][overrides]") {
  Turret turret;
  json scene;
  core::Uuid instanceUuid;
  core::Uuid barrelUuid;
  {
    world::World world;
    turret.load(world);
    const flecs::entity instance = world.instantiate(world.find(turret.root), "Turret 1");
    instanceUuid = world.uuidOf(instance);
    const flecs::entity barrel = barrelOf(world, instance);
    barrelUuid = world.uuidOf(barrel);
    setProperties(instance, 0, {{"speed", 6}, {"range", 5}});
    setProperties(barrel, 0, {{"speed", 7}, {"range", 5}});
    instance.ensure<world::PointLight>().intensity = 4.0f;

    scene = world::saveScene(world);
    INFO(scene.dump(2));
    // Only what changed, keyed by script; the light's other members are left out.
    const json expected = {{"slots", {{Mover.toString(), {{{"properties", {{"speed", 6}}}}}}}}};
    REQUIRE(componentsOf(scene, instanceUuid)["Scripts"] == expected);
    REQUIRE(componentsOf(scene, instanceUuid)["PointLight"] == json{{"intensity", 4.0}});
    REQUIRE(componentsOf(scene, barrelUuid)["Scripts"] ==
            json{{"slots", {{Mover.toString(), {{{"properties", {{"speed", 7}}}}}}}}});
    // The root's transform places the instance and stays whole.
    REQUIRE(componentsOf(scene, instanceUuid)["Transform"].contains("scale"));
  }
  // The prefab changes another property, the other slot and another member of the light.
  turret.slots[0]["properties"]["range"] = 9;
  turret.slots[1]["properties"] = {{"rate", 3}};
  turret.lightRange = 20.0f;
  world::World world;
  turret.load(world);
  REQUIRE(world::loadScene(world, scene).has_value());
  for (const core::Uuid &uuid : {instanceUuid, barrelUuid}) {
    const Slots slots = world.find(uuid).get<Slots>();
    REQUIRE(slots.slots.size() == 2);
    REQUIRE(properties(slots.slots[0])["speed"] == (uuid == instanceUuid ? 6 : 7));
    REQUIRE(properties(slots.slots[0])["range"] == 9);
    REQUIRE(properties(slots.slots[1]) == json{{"rate", 3}});
  }
  const world::PointLight light = world.find(instanceUuid).get<world::PointLight>();
  REQUIRE(light.intensity == Approx(4.0f));
  REQUIRE(light.range == Approx(20.0f));
  // Saving again writes the same changes.
  REQUIRE(componentsOf(world::saveScene(world), instanceUuid) == componentsOf(scene, instanceUuid));
  REQUIRE(componentsOf(world::saveScene(world), barrelUuid) == componentsOf(scene, barrelUuid));
}

TEST_CASE("an instance's slot changes follow their script when the prefab reorders, adds and removes slots",
          "[world][prefab][overrides]") {
  Turret turret;
  json scene;
  core::Uuid instanceUuid;
  {
    world::World world;
    turret.load(world);
    const flecs::entity instance = world.instantiate(world.find(turret.root), "Turret 1");
    instanceUuid = world.uuidOf(instance);
    setProperties(instance, 0, {{"speed", 6}, {"range", 5}});
    setProperties(instance, 1, {{"rate", 8}});
    scene = world::saveScene(world);
  }
  // A slot added ahead and the two swapped.
  const json mover = turret.slots[0];
  const json shooter = turret.slots[1];
  turret.slots = json::array({{{"script", Blinker.toString()}, {"properties", json::object()}}, shooter, mover});
  {
    world::World world;
    turret.load(world);
    REQUIRE(world::loadScene(world, scene).has_value());
    const Slots slots = world.find(instanceUuid).get<Slots>();
    REQUIRE(slots.slots.size() == 3);
    REQUIRE(slots.slots[0].script == Blinker);
    REQUIRE(properties(slots.slots[0]).empty());
    REQUIRE(properties(slots.slots[1]) == json{{"rate", 8}});
    REQUIRE(properties(slots.slots[2])["speed"] == 6);
  }
  // The shooter's slot leaves the prefab: its change is skipped and gone after the next save.
  turret.slots = json::array({mover});
  world::World world;
  turret.load(world);
  REQUIRE(world::loadScene(world, scene).has_value());
  const Slots slots = world.find(instanceUuid).get<Slots>();
  REQUIRE(slots.slots.size() == 1);
  REQUIRE(properties(slots.slots[0])["speed"] == 6);
  REQUIRE(componentsOf(world::saveScene(world), instanceUuid)["Scripts"] ==
          json{{"slots", {{Mover.toString(), {{{"properties", {{"speed", 6}}}}}}}}});
}

TEST_CASE("two slots running one script are paired by their position among that script's slots",
          "[world][prefab][overrides]") {
  Turret turret;
  turret.slots = json::array({{{"script", Mover.toString()}, {"properties", {{"speed", 1}}}},
                              {{"script", Shooter.toString()}, {"properties", json::object()}},
                              {{"script", Mover.toString()}, {"properties", {{"speed", 2}}}}});
  json scene;
  core::Uuid instanceUuid;
  {
    world::World world;
    turret.load(world);
    const flecs::entity instance = world.instantiate(world.find(turret.root), "Turret 1");
    instanceUuid = world.uuidOf(instance);
    setProperties(instance, 2, {{"speed", 5}});
    scene = world::saveScene(world);
    // The first mover is unchanged and holds its place with an empty change.
    REQUIRE(componentsOf(scene, instanceUuid)["Scripts"] ==
            json{{"slots", {{Mover.toString(), {json::object(), {{"properties", {{"speed", 5}}}}}}}}});
  }
  // The shooter moves to the front: the second mover is still the second.
  turret.slots = json::array({turret.slots[1], turret.slots[0], turret.slots[2]});
  turret.slots[1]["properties"]["speed"] = 3;
  world::World world;
  turret.load(world);
  REQUIRE(world::loadScene(world, scene).has_value());
  const Slots slots = world.find(instanceUuid).get<Slots>();
  REQUIRE(properties(slots.slots[1])["speed"] == 3);
  REQUIRE(properties(slots.slots[2])["speed"] == 5);
}

TEST_CASE("a property reset to the script's default and a cleared property are changes of their own",
          "[world][prefab][overrides]") {
  Turret turret;
  const std::string target = core::Uuid::generate().toString();
  turret.slots[1]["properties"] = {{"target", target}};
  json scene;
  core::Uuid instanceUuid;
  {
    world::World world;
    turret.load(world);
    const flecs::entity instance = world.instantiate(world.find(turret.root), "Turret 1");
    instanceUuid = world.uuidOf(instance);
    setProperties(instance, 0, {{"range", 5}}); // speed back to the script's default
    setProperties(instance, 1, {{"target", nullptr}});
    scene = world::saveScene(world);
    INFO(scene.dump(2));
    const json expected = {{"slots",
                            {{Mover.toString(), {{{"properties", {{"$removed", {"speed"}}}}}}},
                             {Shooter.toString(), {{{"properties", {{"target", nullptr}}}}}}}}};
    REQUIRE(componentsOf(scene, instanceUuid)["Scripts"] == expected);
  }
  turret.slots[0]["properties"]["range"] = 9;
  world::World world;
  turret.load(world);
  REQUIRE(world::loadScene(world, scene).has_value());
  const Slots slots = world.find(instanceUuid).get<Slots>();
  REQUIRE(properties(slots.slots[0]) == json{{"range", 9}});
  REQUIRE(properties(slots.slots[1]) == json{{"target", nullptr}});
}

TEST_CASE("an instance that reshapes its slot list keeps it whole", "[world][prefab][overrides]") {
  Turret turret;
  json scene;
  core::Uuid instanceUuid;
  {
    world::World world;
    turret.load(world);
    const flecs::entity instance = world.instantiate(world.find(turret.root), "Turret 1");
    instanceUuid = world.uuidOf(instance);
    instance.ensure<Slots>().slots.push_back({.script = Blinker, .properties = {}});
    instance.modified<Slots>();
    scene = world::saveScene(world);
    REQUIRE(componentsOf(scene, instanceUuid)["Scripts"]["slots"].is_array());
  }
  turret.slots[0]["properties"]["range"] = 9;
  world::World world;
  turret.load(world);
  REQUIRE(world::loadScene(world, scene).has_value());
  const Slots slots = world.find(instanceUuid).get<Slots>();
  REQUIRE(slots.slots.size() == 3);
  REQUIRE(properties(slots.slots[0])["range"] == 5); // the list is the instance's own
}

TEST_CASE("a version 8 scene's instance components replace the prefab's", "[world][prefab][overrides][migration]") {
  const Turret turret;
  const core::Uuid instanceUuid = core::Uuid::generate();
  // What version 8 wrote after the instance changed one property: the whole component.
  const json old = {{"version", 8},
                    {"entities",
                     {{{"uuid", instanceUuid.toString()},
                       {"name", "Turret 1"},
                       {"prefab", turret.root.toString()},
                       {"components",
                        {{"Scripts",
                          {{"slots",
                            {{{"script", Mover.toString()}, {"properties", {{"speed", 6}}}},
                             {{"script", Shooter.toString()}, {"properties", json::object()}}}}}}}}}}}};
  world::World world;
  turret.load(world);
  REQUIRE(world::loadScene(world, old).has_value());
  // Merging would bring back the range the instance had reset to the script's default.
  REQUIRE(properties(world.find(instanceUuid).get<Slots>().slots[0]) == json{{"speed", 6}});
  // Saved again, the same values as a change.
  REQUIRE(componentsOf(world::saveScene(world), instanceUuid)["Scripts"] ==
          json{{"slots", {{Mover.toString(), {{{"properties", {{"$removed", {"range"}}, {"speed", 6}}}}}}}}});
}

TEST_CASE("a patch keeps only the members that differ and merges back onto its source", "[world][overrides]") {
  world::World world;
  registerSlots(world);
  const world::ComponentInfo &scripts = *world.findComponent("Scripts");
  const json source = {{"slots", {{{"script", "a"}, {"properties", {{"x", 1}, {"v", {{"x", 1}, {"y", 2}}}}}}}}};
  const json value = {{"slots", {{{"script", "a"}, {"properties", {{"x", 1}, {"v", {{"x", 3}, {"y", 2}}}}}}}}};
  const json patch = world::componentPatch(scripts, value, source);
  // A value inside embedded JSON is written whole, so it never merges onto half of another.
  REQUIRE(patch == json{{"slots", {{"a", {{{"properties", {{"v", {{"x", 3}, {"y", 2}}}}}}}}}}});
  REQUIRE(world::mergePatch(scripts, source, patch, "test") == value);
  REQUIRE(world::componentPatch(scripts, value, value).empty());
  // A patch onto a list it has nothing to pair with leaves the list alone.
  const json other = {{"slots", {{{"script", "b"}, {"properties", json::object()}}}}};
  REQUIRE(world::mergePatch(scripts, other, patch, "test") == other);
}
