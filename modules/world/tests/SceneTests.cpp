#include <sonnet/world/DrawList.h>
#include <sonnet/world/Scene.h>
#include <sonnet/world/World.h>

#include <sonnet/assets/Bundle.h>
#include <sonnet/core/Log.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/gtc/epsilon.hpp>
#include <glm/gtc/quaternion.hpp>
#include <spdlog/sinks/ostream_sink.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <vector>

using namespace sonnet;
using Catch::Approx;

namespace {

std::filesystem::path temporaryDirectory() {
  const std::filesystem::path path = std::filesystem::temp_directory_path() / "sonnet_world_tests";
  std::filesystem::create_directories(path);
  return path;
}

// One member of each reflected vector type, and a repeated one.
struct Shape {
  glm::vec2 size{};
  glm::vec3 offset{};
  glm::vec4 tint{};
  glm::quat turn{1.0f, 0.0f, 0.0f, 0.0f};
  std::vector<glm::vec3> path{};
};

void registerShape(world::World &world) {
  world.registerVector<glm::vec3>();
  world.registerComponent<Shape>("Shape")
      .member<glm::vec2>("size")
      .member<glm::vec3>("offset")
      .member<glm::vec4>("tint")
      .member<glm::quat>("turn")
      .member<std::vector<glm::vec3>>("path");
}

// Whether `json` is an array of `values`; flecs writes a float to about ten digits.
bool isArray(const nlohmann::json &json, std::initializer_list<float> values) {
  if (!json.is_array() || json.size() != values.size()) {
    return false;
  }
  std::size_t i = 0;
  for (const float value : values) {
    const nlohmann::json &element = json[i];
    ++i;
    if (!element.is_number() || element.get<float>() != Approx(value)) {
      return false;
    }
  }
  return true;
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
    REQUIRE(prefab->get<world::PrefabFile>().path == prefabPath);

    const flecs::entity instance = editing.instantiate(*prefab, "Crate 1");
    REQUIRE(!instance.has<world::PrefabFile>()); // the file is the prefab's, not inherited
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
  {
    // Loaded with others, the prefab knows its file too; from memory it has none.
    world::World listing;
    REQUIRE(world::loadPrefabFiles(listing, std::span{&prefabPath, 1}).empty());
    REQUIRE(listing.find(prefabUuid).get<world::PrefabFile>().path == prefabPath);
    world::World memory;
    const auto fromMemory = world::loadPrefab(memory, world::savePrefab(listing, listing.find(prefabUuid)));
    REQUIRE(fromMemory.has_value());
    REQUIRE(!fromMemory->has<world::PrefabFile>());
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
  REQUIRE(world.uuidOf(roots[0]) == core::Uuid::derive(modelUuid, "path/Body"));
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

TEST_CASE("a model node's camera becomes a Camera that play mode can look through", "[world][scene][prefab]") {
  const core::Uuid modelUuid = core::Uuid::generate();
  assets::Model model;
  model.nodes.push_back({.name = "Set", .parent = -1});
  model.nodes.push_back({.name = "Shot",
                         .parent = 0,
                         .position = {0.0f, 1.0f, 5.0f},
                         .camera = assets::ModelCamera{.fovY = 0.6f, .nearPlane = 0.05f}});
  SECTION("orthographic camera") {
    model.nodes[1].camera->projectionKind = renderer::ProjectionKind::Orthographic;
    model.nodes[1].camera->orthoHeight = 12.0f;
    model.nodes[1].camera->farPlane = 75.0f;
  }
  SECTION("perspective camera") {
  }
  world::World world;
  const flecs::entity prefab = world::loadModelPrefab(world, model, modelUuid, "Stage");
  const flecs::entity shot = world.children(world.children(prefab)[0])[0];
  REQUIRE(shot.get<world::Camera>().fovY == Approx(0.6f));
  REQUIRE(shot.get<world::Camera>().nearPlane == Approx(0.05f));
  REQUIRE(!world.children(prefab)[0].has<world::Camera>());

  // An instance's camera is the scene camera the player and the editor's play mode render from.
  REQUIRE(!world::sceneCamera(world).has_value());
  static_cast<void>(world.instantiate(prefab, "Stage 1"));
  world.progress(0.016f);
  const std::optional<renderer::Camera> camera = world::sceneCamera(world);
  REQUIRE(camera.has_value());
  REQUIRE(camera->position.y == Approx(1.0f));
  REQUIRE(camera->position.z == Approx(5.0f));
  REQUIRE(camera->fovY == Approx(0.6f));
  REQUIRE(camera->nearPlane == Approx(0.05f));
  REQUIRE(camera->projectionKind == model.nodes[1].camera->projectionKind);
  REQUIRE(camera->orthoHeight == Approx(model.nodes[1].camera->orthoHeight));
  REQUIRE(camera->farPlane == Approx(model.nodes[1].camera->farPlane));
}

TEST_CASE("a model node's light becomes the light component of its type", "[world][scene][prefab]") {
  const core::Uuid modelUuid = core::Uuid::generate();
  assets::Model model;
  model.nodes.push_back({.name = "Set", .parent = -1});
  model.nodes.push_back({.name = "Sun",
                         .parent = 0,
                         .light = assets::ModelLight{.type = assets::ModelLight::Type::Directional,
                                                     .color = {1.0f, 0.9f, 0.8f},
                                                     .intensity = 3.0f}});
  model.nodes.push_back(
      {.name = "Bulb",
       .parent = 0,
       .position = {2.0f, 1.0f, 0.0f},
       .light = assets::ModelLight{
           .type = assets::ModelLight::Type::Point, .color = {0.0f, 1.0f, 0.0f}, .intensity = 25.0f, .range = 6.0f}});
  model.nodes.push_back({.name = "Spot",
                         .parent = 0,
                         .light = assets::ModelLight{.type = assets::ModelLight::Type::Spot,
                                                     .intensity = 16.0f,
                                                     .range = 40.0f,
                                                     .innerAngle = 0.2f,
                                                     .outerAngle = 0.5f}});
  world::World world;
  const flecs::entity prefab = world::loadModelPrefab(world, model, modelUuid, "Stage");
  const std::vector<flecs::entity> lamps = world.children(world.children(prefab)[0]);
  REQUIRE(lamps[0].get<world::DirectionalLight>().intensity == Approx(3.0f));
  REQUIRE(lamps[0].get<world::DirectionalLight>().color == glm::vec3{1.0f, 0.9f, 0.8f});
  REQUIRE(lamps[1].get<world::PointLight>().range == Approx(6.0f));
  REQUIRE(lamps[1].get<world::PointLight>().color == glm::vec3{0.0f, 1.0f, 0.0f});
  REQUIRE(!lamps[1].get<world::PointLight>().castsShadows);
  REQUIRE(lamps[2].get<world::SpotLight>().innerAngle == Approx(0.2f));
  REQUIRE(lamps[2].get<world::SpotLight>().outerAngle == Approx(0.5f));
  REQUIRE(!lamps[2].has<world::PointLight>());

  // An instance's lights reach the renderer's light list, placed and aimed by their nodes.
  static_cast<void>(world.instantiate(prefab, "Stage 1"));
  world.progress(0.016f);
  std::vector<renderer::Light> lights;
  world::buildLightList(world, lights);
  REQUIRE(lights.size() == 2);
  const auto point = std::ranges::find(lights, renderer::LightType::Point, &renderer::Light::type);
  REQUIRE(point != lights.end());
  REQUIRE(point->position == glm::vec3{2.0f, 1.0f, 0.0f});
  REQUIRE(point->intensity == Approx(25.0f));
  const auto spot = std::ranges::find(lights, renderer::LightType::Spot, &renderer::Light::type);
  REQUIRE(spot != lights.end());
  REQUIRE(spot->direction.z == Approx(-1.0f));
  REQUIRE(spot->range == Approx(40.0f));
}

TEST_CASE("siblings keep their order through repeated save and load", "[world][scene]") {
  world::World world;
  std::vector<std::string> names;
  for (int i = 0; i < 12; ++i) {
    names.push_back("Root " + std::to_string(i));
    const flecs::entity root = world.createEntity(names.back());
    world.createEntity("A", root);
    world.createEntity("B", root);
  }
  const auto rootNames = [&] {
    std::vector<std::string> result;
    for (const flecs::entity root : world.roots()) {
      result.push_back(root.get<world::Name>().value);
    }
    return result;
  };
  for (int round = 0; round < 3; ++round) {
    const nlohmann::json scene = world::saveScene(world);
    for (const flecs::entity root : world.roots()) {
      world.destroyEntity(root); // frees the ids in an order flecs is free to reuse
    }
    REQUIRE(world::loadScene(world, scene));
    REQUIRE(rootNames() == names);
    const std::vector<flecs::entity> kids = world.children(world.roots().front());
    REQUIRE(kids.size() == 2);
    REQUIRE(kids[0].get<world::Name>().value == "A");
    REQUIRE(kids[1].get<world::Name>().value == "B");
  }
}

TEST_CASE("a sibling moved by index keeps its place through save and load", "[world][scene]") {
  world::World world;
  const flecs::entity a = world.createEntity("A");
  const flecs::entity b = world.createEntity("B");
  const flecs::entity c = world.createEntity("C");
  world.createEntity("A1", a);
  const flecs::entity a2 = world.createEntity("A2", a);
  const auto names = [&](const std::vector<flecs::entity> &entities) {
    std::vector<std::string> result;
    result.reserve(entities.size());
    for (const flecs::entity entity : entities) {
      result.push_back(entity.get<world::Name>().value);
    }
    return result;
  };

  world.setSiblingIndex(c, 0);
  REQUIRE(names(world.roots()) == std::vector<std::string>{"C", "A", "B"});
  REQUIRE(world.siblingIndex(c) == 0);
  REQUIRE(world.siblingIndex(b) == 2);
  world.setSiblingIndex(c, 1);
  REQUIRE(names(world.roots()) == std::vector<std::string>{"A", "C", "B"});
  world.setSiblingIndex(a, 99); // past the end: last
  REQUIRE(names(world.roots()) == std::vector<std::string>{"C", "B", "A"});
  world.setSiblingIndex(a2, 0);
  REQUIRE(names(world.children(a)) == std::vector<std::string>{"A2", "A1"});

  for (int round = 0; round < 2; ++round) {
    const nlohmann::json scene = world::saveScene(world);
    for (const flecs::entity root : world.roots()) {
      world.destroyEntity(root);
    }
    REQUIRE(world::loadScene(world, scene));
    REQUIRE(names(world.roots()) == std::vector<std::string>{"C", "B", "A"});
    REQUIRE(names(world.children(world.roots().back())) == std::vector<std::string>{"A2", "A1"});
  }
}

TEST_CASE("a point light's castsShadows survives a scene round trip and defaults to false", "[world][scene]") {
  world::World source;
  source.createEntity("Shadowed").set<world::PointLight>({.castsShadows = true});
  source.createEntity("Plain").set<world::PointLight>({});
  const nlohmann::json scene = world::saveScene(source);

  world::World target;
  REQUIRE(world::loadScene(target, scene));
  std::size_t count = 0;
  target.ecs().each([&](const world::Name &name, const world::PointLight &light) {
    ++count;
    REQUIRE(light.castsShadows == (name.value == "Shadowed"));
  });
  REQUIRE(count == 2);

  // A scene written before the field existed loads with it off.
  nlohmann::json old = scene;
  for (auto &entity : old["entities"]) {
    entity["components"]["PointLight"].erase("castsShadows");
  }
  world::World legacy;
  REQUIRE(world::loadScene(legacy, old));
  legacy.ecs().each([&](const world::PointLight &light) { REQUIRE(!light.castsShadows); });
}

TEST_CASE("a spot light's castsShadows survives a scene round trip and defaults to false", "[world][scene]") {
  world::World source;
  source.createEntity("Shadowed").set<world::SpotLight>({.castsShadows = true});
  source.createEntity("Plain").set<world::SpotLight>({});
  const nlohmann::json scene = world::saveScene(source);

  world::World target;
  REQUIRE(world::loadScene(target, scene));
  std::vector<bool> flags;
  target.ecs().each([&](const world::Name &name, const world::SpotLight &light) {
    flags.push_back(light.castsShadows);
    REQUIRE(light.castsShadows == (name.value == "Shadowed"));
  });
  REQUIRE(flags.size() == 2);

  // A scene written before the field existed loads with it off.
  nlohmann::json old = scene;
  for (auto &entity : old["entities"]) {
    entity["components"]["SpotLight"].erase("castsShadows");
  }
  world::World legacy;
  REQUIRE(world::loadScene(legacy, old));
  legacy.ecs().each([&](const world::SpotLight &light) { REQUIRE(!light.castsShadows); });
}

TEST_CASE("vector and quaternion members save as arrays and load back (ADR-0029)", "[world][scene]") {
  world::World source;
  registerShape(source);
  const glm::quat turn = glm::angleAxis(glm::radians(30.0f), glm::normalize(glm::vec3{1.0f, 2.0f, 3.0f}));
  const flecs::entity entity = source.createEntity("Shape");
  entity.set<Shape>({.size = {1.5f, 2.5f},
                     .offset = {-1.0f, 0.25f, 8.0f},
                     .tint = {0.1f, 0.2f, 0.3f, 0.4f},
                     .turn = turn,
                     .path = {{1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}}});
  entity.set<world::Transform>({.position = {0.0f, 0.5f, -10.0f}, .rotation = turn, .scale = {20.0f, 1.0f, 20.0f}});

  const nlohmann::json scene = world::saveScene(source);
  REQUIRE(scene["version"] == world::SceneVersion);
  const nlohmann::json &components = scene["entities"][0]["components"];
  REQUIRE(isArray(components["Shape"]["size"], {1.5f, 2.5f}));
  REQUIRE(isArray(components["Shape"]["offset"], {-1.0f, 0.25f, 8.0f}));
  REQUIRE(isArray(components["Shape"]["tint"], {0.1f, 0.2f, 0.3f, 0.4f}));
  REQUIRE(components["Shape"]["path"].size() == 2);
  REQUIRE(isArray(components["Shape"]["path"][0], {1.0f, 2.0f, 3.0f}));
  REQUIRE(isArray(components["Shape"]["path"][1], {4.0f, 5.0f, 6.0f}));
  REQUIRE(isArray(components["Transform"]["position"], {0.0f, 0.5f, -10.0f}));
  REQUIRE(isArray(components["Transform"]["scale"], {20.0f, 1.0f, 20.0f}));
  // x, y, z, w as glTF writes it, whatever order glm stores the quaternion in.
  for (const char *member : {"turn", "rotation"}) {
    const nlohmann::json &rotation = components[member == std::string_view{"turn"} ? "Shape" : "Transform"][member];
    REQUIRE(rotation.size() == 4);
    REQUIRE(rotation[0].get<float>() == Approx(turn.x));
    REQUIRE(rotation[1].get<float>() == Approx(turn.y));
    REQUIRE(rotation[2].get<float>() == Approx(turn.z));
    REQUIRE(rotation[3].get<float>() == Approx(turn.w));
  }

  world::World target;
  registerShape(target);
  REQUIRE(world::loadScene(target, scene).has_value());
  const flecs::entity loaded = target.find(source.uuidOf(entity));
  const Shape &shape = loaded.get<Shape>();
  REQUIRE(shape.size == glm::vec2{1.5f, 2.5f});
  REQUIRE(shape.offset == glm::vec3{-1.0f, 0.25f, 8.0f});
  REQUIRE(glm::all(glm::epsilonEqual(shape.tint, glm::vec4{0.1f, 0.2f, 0.3f, 0.4f}, 1e-6f)));
  REQUIRE(glm::all(glm::epsilonEqual(shape.turn, turn, 1e-6f)));
  REQUIRE(shape.path == std::vector<glm::vec3>{{1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}});
  REQUIRE(glm::all(glm::epsilonEqual(loaded.get<world::Transform>().rotation, turn, 1e-6f)));
  REQUIRE(world::saveScene(target) == scene); // the same text again: a save after a load changes nothing
}

TEST_CASE("a version 4 scene and prefab load their vector objects unchanged", "[world][scene][migration]") {
  const nlohmann::json prefab = nlohmann::json::parse(R"({
    "version": 4,
    "entities": [
      {"uuid": "1b6e0c1a-0004-4a5b-8c9d-000000000001", "name": "Crate",
       "components": {"MeshRenderer": {"color": {"x": 1, "y": 0, "z": 0, "w": 1}}}},
      {"uuid": "1b6e0c1a-0004-4a5b-8c9d-000000000002", "name": "Lid", "parent": "1b6e0c1a-0004-4a5b-8c9d-000000000001",
       "components": {"Transform": {"position": {"x": 0, "y": 0.5, "z": 0}}}}
    ]
  })");
  world::World world;
  const auto crate = world::loadPrefab(world, prefab);
  REQUIRE(crate.has_value());
  REQUIRE(crate->get<world::MeshRenderer>().color == glm::vec4{1.0f, 0.0f, 0.0f, 1.0f});

  const nlohmann::json scene = nlohmann::json::parse(R"({
    "version": 4,
    "entities": [
      {"uuid": "1b6e0c1a-0004-4a5b-8c9d-000000000010", "name": "Crate 1", "prefab": "1b6e0c1a-0004-4a5b-8c9d-000000000001",
       "components": {"Transform": {"position": {"x": 4, "y": 0, "z": 0}, "rotation": {"x": 0, "y": 0.6, "z": 0, "w": 0.8}}}},
      {"uuid": "1b6e0c1a-0004-4a5b-8c9d-000000000011", "name": "Ball",
       "components": {"Transform": {"position": {"x": 1, "y": 2, "z": 3}, "scale": {"x": 2, "y": 2, "z": 2}}}}
    ]
  })");
  std::ostringstream log;
  const auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(log, true);
  core::Log::addSink(sink);
  const auto loaded = world::loadScene(world, scene);
  core::Log::removeSink(sink);
  REQUIRE(loaded.has_value());
  REQUIRE(log.str().find("not an array") == std::string::npos);
  const world::Transform &instance = loaded->at(0).get<world::Transform>();
  REQUIRE(instance.position == glm::vec3{4.0f, 0.0f, 0.0f});
  REQUIRE(instance.rotation.y == Approx(0.6f));
  REQUIRE(instance.rotation.w == Approx(0.8f));
  REQUIRE(loaded->at(1).get<world::Transform>().position == glm::vec3{1.0f, 2.0f, 3.0f});
  REQUIRE(loaded->at(1).get<world::Transform>().scale == glm::vec3{2.0f});
  const flecs::entity lid = world.children(loaded->at(0)).front();
  REQUIRE(lid.get<world::Transform>().position == glm::vec3{0.0f, 0.5f, 0.0f});

  // Saved again, the scene is at the current version and its vectors are arrays.
  const nlohmann::json saved = world::saveScene(world);
  REQUIRE(saved["version"] == world::SceneVersion);
  REQUIRE(isArray(saved["entities"][0]["components"]["Transform"]["position"], {4.0f, 0.0f, 0.0f}));
  REQUIRE(isArray(saved["entities"][0]["components"]["Transform"]["rotation"], {0.0f, 0.6f, 0.0f, 0.8f}));
  REQUIRE(isArray(world::savePrefab(world, *crate)["entities"][0]["components"]["MeshRenderer"]["color"],
                  {1.0f, 0.0f, 0.0f, 1.0f}));
}

TEST_CASE("a version 5 file with an object for a vector logs a malformed value", "[world][scene]") {
  const nlohmann::json scene = nlohmann::json::parse(R"({
    "version": 5,
    "entities": [
      {"uuid": "1b6e0c1a-0005-4a5b-8c9d-000000000001", "name": "Box",
       "components": {"Transform": {"position": {"x": 1, "y": 2, "z": 3}, "rotation": [0, 0, 1], "scale": [2, 2, 2]}}}
    ]
  })");
  world::World world;
  std::ostringstream log;
  const auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(log, true);
  core::Log::addSink(sink);
  const auto loaded = world::loadScene(world, scene);
  core::Log::removeSink(sink);
  REQUIRE(loaded.has_value());
  const std::string logged = log.str();
  REQUIRE(
      logged.find(R"(component "Transform": member "position" holds {"x":1,"y":2,"z":3}, not an array of 3 numbers)") !=
      std::string::npos);
  REQUIRE(logged.find(R"(component "Transform": member "rotation" holds [0,0,1], not an array of 4 numbers)") !=
          std::string::npos);
  // The malformed members keep their defaults, and the rest of the component still loads.
  const world::Transform &transform = loaded->front().get<world::Transform>();
  REQUIRE(transform.position == glm::vec3{0.0f});
  REQUIRE(transform.rotation == glm::quat{1.0f, 0.0f, 0.0f, 0.0f});
  REQUIRE(transform.scale == glm::vec3{2.0f});
}

TEST_CASE("scene text keeps a vector on one line and is otherwise nlohmann's layout", "[world][scene]") {
  const nlohmann::json document = nlohmann::json::parse(R"({"version": 5, "entities": [
    {"name": "A", "components": {"Transform": {"position": [0, 0.5, -10]}, "Static": null, "Gui": {}},
     "removed": ["Static"], "empty": []}]})");
  REQUIRE(world::sceneText(document) == R"({
  "entities": [
    {
      "components": {
        "Gui": {},
        "Static": null,
        "Transform": {
          "position": [0, 0.5, -10]
        }
      },
      "empty": [],
      "name": "A",
      "removed": [
        "Static"
      ]
    }
  ],
  "version": 5
}
)");
  nlohmann::json withoutVectors = document;
  withoutVectors["entities"][0]["components"].erase("Transform");
  REQUIRE(world::sceneText(withoutVectors) == withoutVectors.dump(2) + "\n");
}

namespace {

assets::Model stageModel(bool withNewFirst) {
  assets::Model model;
  model.nodes.push_back({.name = "Set", .parent = -1});
  if (withNewFirst) {
    model.nodes.push_back({.name = "New", .parent = 0});
  }
  model.nodes.push_back({.name = "A", .parent = 0});
  model.nodes.push_back({.name = "B", .parent = 0});
  return model;
}

flecs::entity child(world::World &world, const flecs::entity &instance, std::string_view name) {
  for (const flecs::entity &node : world.children(world.children(instance)[0])) {
    if (node.get<world::Name>().value == name) {
      return node;
    }
  }
  return {};
}

} // namespace

TEST_CASE("a model node keeps its override when a node is inserted ahead of it (ADR-0030)", "[world][scene][prefab]") {
  const core::Uuid modelUuid = core::Uuid::generate();
  nlohmann::json scene;
  core::Uuid instanceUuid;
  {
    world::World world;
    const flecs::entity prefab = world::loadModelPrefab(world, stageModel(false), modelUuid, "Stage");
    const flecs::entity instance = world.instantiate(prefab, "Stage 1");
    instanceUuid = world.uuidOf(instance);
    child(world, instance, "B").ensure<world::Transform>().position.x = 9.0f;
    scene = world::saveScene(world);
  }
  // The re-exported file has a node ahead of A and B; a fresh world loads the saved scene onto it.
  world::World reopened;
  static_cast<void>(world::loadModelPrefab(reopened, stageModel(true), modelUuid, "Stage"));
  REQUIRE(world::loadScene(reopened, scene).has_value());
  const flecs::entity instance = reopened.find(instanceUuid);
  REQUIRE(instance.is_valid());
  REQUIRE(child(reopened, instance, "B").get<world::Transform>().position.x == Approx(9.0f));
  REQUIRE(child(reopened, instance, "A").get<world::Transform>().position.x == Approx(0.0f));
  REQUIRE(child(reopened, instance, "New").get<world::Transform>().position.x == Approx(0.0f));
}

TEST_CASE("equal sibling names take #n in sibling order and equal names under other parents do not clash",
          "[world][scene][prefab]") {
  const core::Uuid modelUuid = core::Uuid::generate();
  assets::Model model;
  model.nodes.push_back({.name = "Set", .parent = -1});
  model.nodes.push_back({.name = "Lamp", .parent = 0});
  model.nodes.push_back({.name = "Lamp", .parent = 0});
  model.nodes.push_back({.name = "Arm", .parent = 0});
  model.nodes.push_back({.name = "Lamp", .parent = 3});
  world::World world;
  const flecs::entity prefab = world::loadModelPrefab(world, model, modelUuid, "Stage");
  const std::vector<flecs::entity> nodes = world.children(world.children(prefab)[0]);
  REQUIRE(world.uuidOf(nodes[0]) == core::Uuid::derive(modelUuid, "path/Set/Lamp"));
  REQUIRE(world.uuidOf(nodes[1]) == core::Uuid::derive(modelUuid, "path/Set/Lamp#1"));
  REQUIRE(world.uuidOf(world.children(nodes[2])[0]) == core::Uuid::derive(modelUuid, "path/Set/Arm/Lamp"));
}

TEST_CASE("an override saved under the old index identities is skipped with no node changed",
          "[world][scene][prefab]") {
  const core::Uuid modelUuid = core::Uuid::generate();
  world::World world;
  const flecs::entity prefab = world::loadModelPrefab(world, stageModel(false), modelUuid, "Stage");
  const flecs::entity instance = world.instantiate(prefab, "Stage 1");
  child(world, instance, "B").ensure<world::Transform>().position.x = 9.0f;
  nlohmann::json scene = world::saveScene(world);
  // An entry saved under the index scheme names an identity no node has now.
  bool rewritten = false;
  for (nlohmann::json &entry : scene["entities"]) {
    if (entry.value("instanced", false)) {
      entry["uuid"] = core::Uuid::generate().toString();
      rewritten = true;
    }
  }
  REQUIRE(rewritten);

  world::World reopened;
  static_cast<void>(world::loadModelPrefab(reopened, stageModel(false), modelUuid, "Stage"));
  REQUIRE(world::loadScene(reopened, scene).has_value());
  const flecs::entity loaded = reopened.find(world.uuidOf(instance));
  REQUIRE(child(reopened, loaded, "A").get<world::Transform>().position.x == Approx(0.0f));
  REQUIRE(child(reopened, loaded, "B").get<world::Transform>().position.x == Approx(0.0f));
}

TEST_CASE("camera post processing round trips and defaults when absent", "[world][scene][camera]") {
  world::World world;
  const auto camera = world.createEntity("Camera");
  camera.set<world::Camera>({.exposure = 2.5f, .bloomStrength = 0.3f});
  const auto cameraUuid = world.uuidOf(camera);
  const auto saved = world::saveScene(world);
  world.clearScene();
  REQUIRE(world::loadScene(world, saved));
  const auto loaded = world.find(cameraUuid).get<world::Camera>();
  REQUIRE(loaded.exposure == Approx(2.5f));
  REQUIRE(loaded.bloomStrength == Approx(0.3f));
  world.progress(0.0f);
  REQUIRE(world::sceneCamera(world)->exposure == Approx(2.5f));
  REQUIRE(world::sceneCamera(world)->bloomStrength == Approx(0.3f));

  auto old = saved;
  old["entities"][0]["components"]["Camera"].erase("exposure");
  old["entities"][0]["components"]["Camera"].erase("bloomStrength");
  world.clearScene();
  REQUIRE(world::loadScene(world, old));
  REQUIRE(world.find(cameraUuid).get<world::Camera>().exposure == Approx(1.0f));
  REQUIRE(world.find(cameraUuid).get<world::Camera>().bloomStrength == Approx(0.04f));
}

TEST_CASE("version 5 moves the first environment exposure to cameras", "[world][scene][camera]") {
  world::World world;
  const auto first = world.createEntity("First");
  first.set<world::Camera>({});
  const auto second = world.createEntity("Second");
  second.set<world::Camera>({.exposure = 3.0f});
  const auto sky = world.createEntity("Sky");
  sky.set<world::Environment>({});
  const auto other = world.createEntity("Other sky");
  other.set<world::Environment>({});
  const auto firstUuid = world.uuidOf(first);
  const auto secondUuid = world.uuidOf(second);
  auto old = world::saveScene(world);
  old["version"] = 5;
  old["entities"][0]["components"]["Camera"].erase("exposure");
  old["entities"][2]["components"]["Environment"]["exposure"] = 0.5f;
  old["entities"][3]["components"]["Environment"]["exposure"] = 4.0f;
  world.clearScene();
  REQUIRE(world::loadScene(world, old));
  REQUIRE(world.find(firstUuid).get<world::Camera>().exposure == Approx(0.5f));
  REQUIRE(world.find(secondUuid).get<world::Camera>().exposure == Approx(3.0f));
  const auto saved = world::saveScene(world);
  REQUIRE_FALSE(saved["entities"][2]["components"]["Environment"].contains("exposure"));
  REQUIRE_FALSE(saved["entities"][3]["components"]["Environment"].contains("exposure"));

  SECTION("a default first environment wins over later environments") {
    old["entities"][2]["components"]["Environment"].erase("exposure");
    world.clearScene();
    REQUIRE(world::loadScene(world, old));
    REQUIRE(world.find(firstUuid).get<world::Camera>().exposure == Approx(1.0f));
  }
  SECTION("a camera prefab without an environment keeps the defaults") {
    old["entities"].erase(old["entities"].begin() + 1, old["entities"].end());
    world.clearScene();
    const auto prefab = world::loadPrefab(world, old);
    REQUIRE(prefab);
    REQUIRE(prefab->get<world::Camera>().exposure == Approx(1.0f));
    REQUIRE(prefab->get<world::Camera>().bloomStrength == Approx(0.04f));
  }
}

TEST_CASE("orthographic scene cameras retain projection and old cameras default to perspective",
          "[world][scene][camera]") {
  world::World world;
  const auto camera = world.createEntity("Plan");
  camera.set<world::Camera>({.nearPlane = 0.25f,
                             .projectionKind = renderer::ProjectionKind::Orthographic,
                             .orthoHeight = 8.0f,
                             .farPlane = 42.0f});
  const auto uuid = world.uuidOf(camera);
  auto saved = world::saveScene(world);
  REQUIRE(saved["version"] == 7);
  const auto payload = assets::decodeJson(assets::encodeJson(saved));
  REQUIRE(payload);
  world.clearScene();
  REQUIRE(world::loadScene(world, *payload));
  world.progress(0.0f);
  const auto view = world::sceneCamera(world);
  REQUIRE(view);
  REQUIRE(view->projectionKind == renderer::ProjectionKind::Orthographic);
  REQUIRE(view->orthoHeight == Approx(8.0f));
  REQUIRE(view->farPlane == Approx(42.0f));
  REQUIRE(view->nearPlane == Approx(0.25f));
  saved["version"] = 6;
  auto &fields = saved["entities"][0]["components"]["Camera"];
  fields.erase("projectionKind");
  fields.erase("orthoHeight");
  fields.erase("farPlane");
  world.clearScene();
  REQUIRE(world::loadScene(world, saved));
  REQUIRE(world.find(uuid).get<world::Camera>().projectionKind == renderer::ProjectionKind::Perspective);
}
