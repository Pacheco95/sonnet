#include <sonnet/world/Scene.h>
#include <sonnet/world/World.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

using namespace sonnet;
using Catch::Approx;

namespace {

std::filesystem::path temporaryDirectory() {
  const std::filesystem::path path = std::filesystem::temp_directory_path() / "sonnet_world_tests";
  std::filesystem::create_directories(path);
  return path;
}

} // namespace

TEST_CASE("a scene saves and loads with the same identities, hierarchy and components", "[world][scene]") {
  world::World source;
  const flecs::entity ground = source.createEntity("Ground");
  ground.set<world::MeshRenderer>({.mesh = assets::builtin::plane(), .color = {0.4f, 0.4f, 0.4f, 1.0f}});
  ground.add<world::Static>();
  const flecs::entity box = source.createEntity("Box", ground);
  box.set<world::Transform>({.position = {0.0f, 0.5f, 0.0f}});
  box.set<world::Spin>({.speed = 2.0f});
  const flecs::entity light = source.createEntity("Sun");
  light.set<world::DirectionalLight>({.intensity = 4.0f});
  const flecs::entity hidden = source.createEntity("Editor camera");
  hidden.add<world::EditorOnly>();

  const nlohmann::json scene = world::saveScene(source);
  REQUIRE(scene["version"] == world::SceneVersion);
  REQUIRE(scene["entities"].size() == 3); // editor-only entities stay out
  REQUIRE(scene["entities"][0]["name"] == "Ground");
  REQUIRE(scene["entities"][1]["name"] == "Box"); // parents come before children
  REQUIRE(scene["entities"][1]["parent"] == source.uuidOf(ground).toString());
  REQUIRE(scene["entities"][0]["components"]["Static"].is_null());
  REQUIRE(scene["entities"][0]["components"]["MeshRenderer"]["mesh"] == assets::builtin::plane().toString());
  REQUIRE(!scene["entities"][0].contains("prefab"));

  world::World target;
  const auto loaded = world::loadScene(target, scene);
  REQUIRE(loaded.has_value());
  REQUIRE(loaded->size() == 3);
  const flecs::entity loadedBox = target.find(source.uuidOf(box));
  REQUIRE(loadedBox.is_valid());
  REQUIRE(loadedBox.get<world::Name>().value == "Box");
  REQUIRE(target.parentOf(loadedBox) == target.find(source.uuidOf(ground)));
  REQUIRE(loadedBox.get<world::Transform>().position.y == Approx(0.5f));
  REQUIRE(loadedBox.get<world::Spin>().speed == Approx(2.0f));
  REQUIRE(target.find(source.uuidOf(ground)).has<world::Static>());
  REQUIRE(target.find(source.uuidOf(light)).get<world::DirectionalLight>().intensity == Approx(4.0f));
  REQUIRE(!target.find(source.uuidOf(hidden)).is_valid());
  REQUIRE(world::saveScene(target) == scene);

  // Loading the same file twice is refused rather than duplicating identities.
  const auto again = world::loadScene(target, scene);
  REQUIRE(!again.has_value());
  REQUIRE(target.roots().size() == 2);
}

TEST_CASE("scene files reject unknown versions and malformed documents", "[world][scene]") {
  world::World world;
  auto newer = nlohmann::json::parse(R"({"version": 99, "entities": []})");
  REQUIRE(!world::loadScene(world, newer).has_value());
  REQUIRE(world::loadScene(world, newer).error().message.contains("newer"));
  REQUIRE(!world::loadScene(world, nlohmann::json::parse(R"({"entities": []})")).has_value());
  REQUIRE(!world::loadScene(world, nlohmann::json::parse(R"({"version": 1})")).has_value());
  const auto badUuid =
      world::loadScene(world, nlohmann::json::parse(R"({"version": 1, "entities": [{"uuid": "nope"}]})"));
  REQUIRE(!badUuid.has_value());
  REQUIRE(world.roots().empty());

  // A missing parent unwinds what the load created.
  const auto orphan = world::loadScene(
      world,
      nlohmann::json::parse(
          R"({"version": 1, "entities": [{"uuid": "11111111-2222-3333-4444-555555555555", "name": "x", "parent": "99999999-2222-3333-4444-555555555555"}]})"));
  REQUIRE(!orphan.has_value());
  REQUIRE(world.roots().empty());

  const auto missing = world::loadSceneFile(world, temporaryDirectory() / "does-not-exist.scene.json");
  REQUIRE(!missing.has_value());
}

TEST_CASE("a prefab instance saves its overrides only and comes back as an instance", "[world][scene][prefab]") {
  const std::filesystem::path directory = temporaryDirectory();
  const std::filesystem::path prefabPath = directory / "crate.prefab.json";
  const std::filesystem::path scenePath = directory / "main.scene.json";
  core::Uuid prefabUuid;
  core::Uuid instanceUuid;
  {
    world::World authoring;
    const flecs::entity crate = authoring.createEntity("Crate");
    crate.set<world::MeshRenderer>({.mesh = assets::builtin::box(), .color = {1.0f, 0.0f, 0.0f, 1.0f}});
    const flecs::entity lid = authoring.createEntity("Lid", crate);
    lid.set<world::MeshRenderer>({.mesh = assets::builtin::plane()});
    lid.set<world::Transform>({.position = {0.0f, 0.5f, 0.0f}});
    prefabUuid = authoring.uuidOf(crate);
    REQUIRE(world::savePrefabFile(authoring, crate, prefabPath).has_value());
  }
  {
    world::World editing;
    const auto prefab = world::loadPrefabFile(editing, prefabPath);
    REQUIRE(prefab.has_value());
    REQUIRE(prefab->has(flecs::Prefab));
    REQUIRE(editing.uuidOf(*prefab) == prefabUuid);
    REQUIRE(editing.roots().empty());

    const flecs::entity instance = editing.instantiate(*prefab, "Crate 1");
    instance.ensure<world::Transform>().position = {4.0f, 0.0f, 0.0f};
    instance.ensure<world::MeshRenderer>().color = {0.0f, 0.0f, 1.0f, 1.0f};
    instance.add<world::Static>();
    instanceUuid = editing.uuidOf(instance);
    REQUIRE(editing.children(instance).size() == 1);

    const nlohmann::json scene = world::saveScene(editing);
    REQUIRE(scene["entities"].size() == 1); // the instantiated child is not written
    const nlohmann::json &entry = scene["entities"][0];
    REQUIRE(entry["prefab"] == prefabUuid.toString());
    REQUIRE(entry["components"].contains("Transform"));
    REQUIRE(entry["components"].contains("MeshRenderer"));
    REQUIRE(entry["components"].contains("Static"));
    REQUIRE(!entry["components"].contains("Spin"));
    REQUIRE(world::saveSceneFile(editing, scenePath).has_value());
  }
  {
    world::World playing;
    // Without the prefab the scene cannot load, and says which one is missing.
    const auto withoutPrefab = world::loadSceneFile(playing, scenePath);
    REQUIRE(!withoutPrefab.has_value());
    REQUIRE(withoutPrefab.error().message.contains(prefabUuid.toString()));

    REQUIRE(world::loadPrefabFile(playing, prefabPath).has_value());
    const auto loaded = world::loadSceneFile(playing, scenePath);
    REQUIRE(loaded.has_value());
    const flecs::entity instance = playing.find(instanceUuid);
    REQUIRE(instance.is_valid());
    REQUIRE(playing.isInstance(instance));
    REQUIRE(instance.get<world::Transform>().position.x == Approx(4.0f));
    REQUIRE(instance.get<world::MeshRenderer>().color.b == Approx(1.0f));
    REQUIRE(instance.has<world::Static>());
    const std::vector<flecs::entity> children = playing.children(instance);
    REQUIRE(children.size() == 1);
    REQUIRE(children[0].get<world::MeshRenderer>().mesh == assets::builtin::plane());
    REQUIRE(playing.roots().size() == 1);
  }
  std::filesystem::remove(prefabPath);
  std::filesystem::remove(scenePath);
}

TEST_CASE("a version 1 scene migrates its primitives to built-in mesh identities", "[world][scene][migration]") {
  const nlohmann::json old = nlohmann::json::parse(R"({
    "version": 1,
    "entities": [
      {"uuid": "1b6e0c1a-0001-4a5b-8c9d-000000000001", "name": "Ground",
       "components": {"MeshRenderer": {"primitive": "Plane", "color": {"x": 0.5, "y": 0.5, "z": 0.5, "w": 1.0}, "visible": true}}},
      {"uuid": "1b6e0c1a-0001-4a5b-8c9d-000000000002", "name": "Thing",
       "components": {"MeshRenderer": {"primitive": "Capsule", "color": {"x": 1, "y": 1, "z": 1, "w": 1}, "visible": false}}}
    ]
  })");
  world::World world;
  const auto loaded = world::loadScene(world, old);
  REQUIRE(loaded.has_value());
  const flecs::entity ground = world.find(core::Uuid::parse("1b6e0c1a-0001-4a5b-8c9d-000000000001").value());
  REQUIRE(ground.get<world::MeshRenderer>().mesh == assets::builtin::plane());
  REQUIRE(ground.get<world::MeshRenderer>().color.r == Approx(0.5f));
  const flecs::entity thing = world.find(core::Uuid::parse("1b6e0c1a-0001-4a5b-8c9d-000000000002").value());
  REQUIRE(thing.get<world::MeshRenderer>().mesh == assets::builtin::capsule());
  REQUIRE(!thing.get<world::MeshRenderer>().visible);
  // Saved again, the scene is at the current version and names meshes by identity.
  const nlohmann::json saved = world::saveScene(world);
  REQUIRE(saved["version"] == world::SceneVersion);
  REQUIRE(saved["entities"][0]["components"]["MeshRenderer"]["mesh"] == assets::builtin::plane().toString());
  REQUIRE(!saved["entities"][0]["components"]["MeshRenderer"].contains("primitive"));
  nlohmann::json future = old;
  future["version"] = world::SceneVersion + 1;
  REQUIRE(!world::loadScene(world, future).has_value());
}

TEST_CASE("a model becomes a prefab with its node hierarchy and meshes", "[world][scene][prefab]") {
  const core::Uuid modelUuid = core::Uuid::generate();
  const core::Uuid meshUuid = core::Uuid::derive(modelUuid, "mesh/0");
  assets::Model model;
  model.nodes.push_back({.name = "Body", .parent = -1, .position = {1.0f, 0.0f, 0.0f}, .mesh = meshUuid});
  model.nodes.push_back({.name = "Wheel", .parent = 0, .position = {0.0f, -0.5f, 0.0f}, .mesh = meshUuid});
  model.nodes.push_back({.name = "Pivot", .parent = -1});
  world::World world;
  const flecs::entity prefab = world::loadModelPrefab(world, model, modelUuid, "Car");
  REQUIRE(prefab.has(flecs::Prefab));
  REQUIRE(world.uuidOf(prefab) == modelUuid);
  REQUIRE(prefab.get<world::Name>().value == "Car");
  const std::vector<flecs::entity> roots = world.children(prefab);
  REQUIRE(roots.size() == 2);
  REQUIRE(roots[0].get<world::Name>().value == "Body");
  REQUIRE(roots[0].get<world::MeshRenderer>().mesh == meshUuid);
  REQUIRE(roots[0].get<world::Transform>().position.x == Approx(1.0f));
  REQUIRE(world.uuidOf(roots[0]) == core::Uuid::derive(modelUuid, "node/0"));
  REQUIRE(world.children(roots[0]).size() == 1);
  REQUIRE(world.children(roots[0])[0].get<world::Name>().value == "Wheel");
  REQUIRE(!roots[1].has<world::MeshRenderer>());
  REQUIRE(world.prefabs().size() == 1);
  REQUIRE(world.roots().empty());

  // Instances place the whole hierarchy and save as a reference to the model's identity.
  const flecs::entity instance = world.instantiate(prefab, "Car 1");
  world.progress(0.016f);
  REQUIRE(world.children(instance).size() == 2);
  const flecs::entity wheel = world.children(world.children(instance)[0])[0];
  REQUIRE(glm::vec3{wheel.get<world::WorldTransform>().matrix[3]}.y == Approx(-0.5f));
  const nlohmann::json scene = world::saveScene(world);
  REQUIRE(scene["entities"].size() == 1);
  REQUIRE(scene["entities"][0]["prefab"] == modelUuid.toString());
}
