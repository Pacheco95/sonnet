#include <sonnet/world/World.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace sonnet;
using Catch::Approx;

TEST_CASE("scene entities carry an identity, a name and transforms and are found by uuid", "[world]") {
  world::World world;
  const flecs::entity entity = world.createEntity("Box");
  REQUIRE(entity.has<world::Identity>());
  REQUIRE(entity.get<world::Name>().value == "Box");
  REQUIRE(entity.has<world::Transform>());
  REQUIRE(entity.has<world::WorldTransform>());
  const core::Uuid uuid = world.uuidOf(entity);
  REQUIRE(!uuid.isNil());
  REQUIRE(world.find(uuid) == entity);

  const core::Uuid given = core::Uuid::generate();
  const flecs::entity child = world.createEntity("Child", entity, given);
  REQUIRE(world.uuidOf(child) == given);
  REQUIRE(world.parentOf(child) == entity);
  REQUIRE(world.children(entity).size() == 1);
  REQUIRE(world.roots().size() == 1);
  REQUIRE(world.isDescendant(child, entity));
  REQUIRE(!world.isDescendant(entity, child));

  REQUIRE(world.fromPickId(world::World::pickId(child)) == child);
  REQUIRE(!world.fromPickId(0).is_valid());

  world.destroyEntity(entity);
  REQUIRE(!world.find(uuid).is_valid());
  REQUIRE(!world.find(given).is_valid()); // children go with the parent
  REQUIRE(world.roots().empty());
}

TEST_CASE("world transforms compose parent times local through the hierarchy", "[world][transform]") {
  world::World world;
  const flecs::entity parent = world.createEntity("Parent");
  parent.set<world::Transform>(
      {.position = {10.0f, 0.0f, 0.0f}, .rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3{0.0f, 1.0f, 0.0f})});
  const flecs::entity child = world.createEntity("Child", parent);
  child.set<world::Transform>({.position = {1.0f, 0.0f, 0.0f}});
  const flecs::entity grandchild = world.createEntity("Grandchild", child);
  grandchild.set<world::Transform>({.position = {0.0f, 2.0f, 0.0f}});

  world.progress(1.0f / 60.0f);
  // The child's +X is rotated onto -Z by the parent, then offset by the parent's position.
  const glm::vec3 childPosition{child.get<world::WorldTransform>().matrix[3]};
  REQUIRE(childPosition.x == Approx(10.0f).margin(1e-5f));
  REQUIRE(childPosition.z == Approx(-1.0f).margin(1e-5f));
  const glm::vec3 grandchildPosition{grandchild.get<world::WorldTransform>().matrix[3]};
  REQUIRE(grandchildPosition.y == Approx(2.0f).margin(1e-5f));
  REQUIRE(grandchildPosition.z == Approx(-1.0f).margin(1e-5f));
}

TEST_CASE("reparenting keeps the world transform", "[world][transform]") {
  world::World world;
  const flecs::entity parent = world.createEntity("Parent");
  parent.set<world::Transform>({.position = {5.0f, 0.0f, 0.0f}, .scale = {2.0f, 2.0f, 2.0f}});
  const flecs::entity entity = world.createEntity("Entity");
  entity.set<world::Transform>({.position = {7.0f, 1.0f, 0.0f}});

  world.setParent(entity, parent);
  REQUIRE(world.parentOf(entity) == parent);
  const world::Transform local = entity.get<world::Transform>();
  REQUIRE(local.position.x == Approx(1.0f));
  REQUIRE(local.position.y == Approx(0.5f));
  REQUIRE(local.scale.x == Approx(0.5f));
  world.progress(0.016f);
  REQUIRE(glm::vec3{entity.get<world::WorldTransform>().matrix[3]} == glm::vec3{7.0f, 1.0f, 0.0f});

  world.setParent(entity, {});
  REQUIRE(!world.parentOf(entity).is_valid());
  REQUIRE(entity.get<world::Transform>().position.x == Approx(7.0f));
  REQUIRE(entity.get<world::Transform>().scale.x == Approx(1.0f));
}

TEST_CASE("simulation systems run only in play mode", "[world][play]") {
  world::World world;
  const flecs::entity spinner = world.createEntity("Spinner");
  spinner.set<world::Spin>({.axis = {0.0f, 1.0f, 0.0f}, .speed = glm::radians(90.0f)});
  const glm::quat initial = spinner.get<world::Transform>().rotation;

  world.progress(1.0f);
  REQUIRE(spinner.get<world::Transform>().rotation == initial);

  world.setPlaying(true);
  REQUIRE(world.isPlaying());
  world.progress(1.0f);
  const glm::quat turned = spinner.get<world::Transform>().rotation;
  REQUIRE(turned != initial);
  REQUIRE(glm::angle(turned) == Approx(glm::radians(90.0f)).margin(1e-4f));
  // World transforms follow within the same progress.
  REQUIRE(spinner.get<world::WorldTransform>().matrix == turned.operator glm::mat4());

  spinner.add<world::Disabled>();
  world.progress(1.0f);
  REQUIRE(spinner.get<world::Transform>().rotation == turned);

  world.setPlaying(false);
  spinner.remove<world::Disabled>();
  world.progress(1.0f);
  REQUIRE(spinner.get<world::Transform>().rotation == turned);
}

TEST_CASE("fixed-update systems step at the fixed timestep in play mode only", "[world][play][fixed]") {
  world::World world({.fixedDelta = 0.25f, .maxFixedSteps = 3});
  std::vector<float> steps;
  std::vector<std::string> order;
  const flecs::entity fixedSystem =
      world.ecs().system("FixedProbe").kind(world.phase(world::Phase::FixedUpdate)).run([&](flecs::iter &it) {
        steps.push_back(it.delta_time());
        order.emplace_back("fixed");
      });
  world.addToSimulation(fixedSystem);
  world.ecs().system("InputProbe").kind(world.phase(world::Phase::Input)).run([&](flecs::iter &) {
    order.emplace_back("input");
  });
  world.ecs().system("UpdateProbe").kind(world.phase(world::Phase::Update)).run([&](flecs::iter &) {
    order.emplace_back("update");
  });

  world.progress(1.0f);
  REQUIRE(steps.empty());
  REQUIRE(order == std::vector<std::string>{"input", "update"});

  world.setPlaying(true);
  order.clear();
  world.progress(0.625f);
  REQUIRE(steps == std::vector<float>{0.25f, 0.25f});
  REQUIRE(order == std::vector<std::string>{"input", "fixed", "fixed", "update"});
  REQUIRE(world.fixedAlpha() == 0.5f);

  // The remainder carries into the next frame.
  world.progress(0.125f);
  REQUIRE(steps.size() == 3);
  REQUIRE(world.fixedAlpha() == Approx(0.0f).margin(1e-4f));

  // A long frame runs at most maxFixedSteps and drops the rest of the backlog.
  steps.clear();
  world.progress(10.0f);
  REQUIRE(steps.size() == 3);
  REQUIRE(world.fixedAlpha() < 1.0f);
  steps.clear();
  world.progress(0.0001f);
  REQUIRE(steps.size() <= 1);

  // Stopping and playing again starts from an empty accumulator.
  world.setPlaying(false);
  world.setPlaying(true);
  REQUIRE(world.fixedAlpha() == 0.0f);
}

TEST_CASE("components registered from outside the world reach the registry and scene JSON", "[world][components]") {
  struct Probe {
    float value{0.0f};
  };
  world::World world;
  world.registerComponent<Probe>("Probe").member<float>("value");
  const world::ComponentInfo *info = world.findComponent("Probe");
  REQUIRE(info != nullptr);
  REQUIRE_FALSE(info->tag);

  const flecs::entity entity = world.createEntity("Probe");
  world.componentFromJson(entity, info->id, nlohmann::json{{"value", 2.5f}});
  REQUIRE(entity.get<Probe>().value == 2.5f);
  REQUIRE(world.componentToJson(entity, info->id) == nlohmann::json{{"value", 2.5f}});
}

TEST_CASE("a prefab instance shares components until it overrides them", "[world][prefab]") {
  world::World world;
  const flecs::entity prefab = world.createEntity("Crate");
  prefab.add(flecs::Prefab);
  prefab.set<world::MeshRenderer>({.mesh = assets::builtin::box(), .color = {1.0f, 0.0f, 0.0f, 1.0f}});
  prefab.set<world::Transform>({.position = {0.0f, 3.0f, 0.0f}});
  const flecs::entity lid = world.createEntity("Lid", prefab);
  lid.add(flecs::Prefab);
  lid.set<world::MeshRenderer>({.mesh = assets::builtin::plane()});
  lid.set<world::Transform>({.position = {0.0f, 0.5f, 0.0f}});

  const flecs::entity instance = world.instantiate(prefab, "Crate 1");
  REQUIRE(world.isInstance(instance));
  REQUIRE(world.prefabOf(instance) == prefab);
  REQUIRE(instance.get<world::Name>().value == "Crate 1");
  REQUIRE(world.uuidOf(instance) != world.uuidOf(prefab));
  REQUIRE(instance.owns<world::Transform>());
  REQUIRE(instance.get<world::Transform>().position.y == Approx(3.0f));
  REQUIRE(instance.has<world::MeshRenderer>());
  REQUIRE(!instance.owns<world::MeshRenderer>());
  REQUIRE(instance.get<world::MeshRenderer>().color.r == Approx(1.0f));

  // The prefab's children come along, with identities and world transforms of their own.
  const std::vector<flecs::entity> children = world.children(instance);
  REQUIRE(children.size() == 1);
  REQUIRE(children[0].get<world::Name>().value == "Lid");
  REQUIRE(children[0].has<world::Identity>());
  REQUIRE(children[0].has<world::WorldTransform>());
  REQUIRE(!world.isInstance(children[0])); // only the root is an instance of a prefab file
  world.progress(0.016f);
  REQUIRE(glm::vec3{children[0].get<world::WorldTransform>().matrix[3]}.y == Approx(3.5f));

  // Editing overrides the component; the prefab keeps its value.
  instance.ensure<world::MeshRenderer>().color = {0.0f, 1.0f, 0.0f, 1.0f};
  REQUIRE(instance.owns<world::MeshRenderer>());
  REQUIRE(prefab.get<world::MeshRenderer>().color.r == Approx(1.0f));

  // Prefabs are not scene roots, instances are.
  const std::vector<flecs::entity> roots = world.roots();
  REQUIRE(roots.size() == 1);
  REQUIRE(roots[0] == instance);
  world.clearScene();
  REQUIRE(world.roots().empty());
  REQUIRE(prefab.is_alive());
  REQUIRE(world.prefabs().size() == 1);
  world.clearPrefabs();
  REQUIRE(!prefab.is_alive());
  REQUIRE(!lid.is_alive());
  REQUIRE(world.prefabs().empty());
}
