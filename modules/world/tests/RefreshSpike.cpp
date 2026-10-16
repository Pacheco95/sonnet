// SPIKE (#304, #305): throwaway. Prototypes refreshing a model prefab and its instances after a
// re-import, two ways, with node identities derived from name paths.
#include "RefreshSpikeSupport.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <map>
#include <string>
#include <vector>

using namespace sonnet;
using Catch::Approx;

namespace {

using namespace sonnet::spike;

// ---- Approach R: save, rebuild the prefab, load. ----
flecs::entity refreshByReload(world::World &world, flecs::entity prefab, const assets::Model &next) {
  const core::Uuid uuid = world.uuidOf(prefab);
  const nlohmann::json scene = world::saveScene(world);
  world.clearScene();
  world.destroyEntity(prefab);
  const flecs::entity rebuilt = buildPrefab(world, next, uuid);
  const auto loaded = world::loadScene(world, scene);
  REQUIRE(loaded.has_value());
  return rebuilt;
}


struct Fixture {
  core::Uuid uuid = core::Uuid::generate();
  world::World world;
  flecs::entity prefab;
  flecs::entity first;
  flecs::entity second;

  explicit Fixture(const assets::Model &model) {
    prefab = buildPrefab(world, model, uuid);
    first = world.instantiate(prefab, "First");
    second = world.instantiate(prefab, "Second");
    world.progress(0.016f);
  }
  flecs::entity node(flecs::entity instance, const char *path) const {
    return world.findByPath(instance, path);
  }
};

enum class How { Reload, Merge };

void refresh(Fixture &f, How how, const assets::Model &next) {
  if (how == How::Reload) {
    f.prefab = refreshByReload(f.world, f.prefab, next);
    f.first = f.world.find(f.world.uuidOf(f.first.is_alive() ? f.first : f.first));
  } else {
    refreshByMerge(f.world, f.prefab, next);
  }
}

} // namespace

TEMPLATE_TEST_CASE_SIG("refresh keeps overrides, updates the rest, handles inserted and removed nodes",
                       "[spike]", ((How H), H), How::Reload, How::Merge) {
  const assets::Model before = modelOf({{"Set", -1, 0.0f}, {"A", 0, 10.0f}, {"B", 0, 10.0f}, {"C", 0, 0.0f, 3.0f}});
  Fixture f{before};
  const core::Uuid firstId = f.world.uuidOf(f.first);
  const core::Uuid secondId = f.world.uuidOf(f.second);
  // The scene overrides B's light on the second instance.
  f.node(f.second, "Set/B").set<world::PointLight>({.color = {1, 1, 1}, .intensity = 99.0f, .range = 5.0f, .castsShadows = false});
  const core::Uuid bId = f.world.uuidOf(f.node(f.second, "Set/B"));

  // Re-export: A dimmer, a node "New" inserted in the middle, C removed.
  const assets::Model after = modelOf({{"Set", -1, 0.0f}, {"A", 0, 4.0f}, {"New", 0, 7.0f}, {"B", 0, 10.0f}});
  refresh(f, H, after);
  f.first = f.world.find(firstId);
  f.second = f.world.find(secondId);
  REQUIRE(f.first);
  REQUIRE(f.second);
  f.world.progress(0.016f);

  // Changed value reaches the untouched instance, and the override survives on the other.
  CHECK(f.node(f.first, "Set/A").get<world::PointLight>().intensity == Approx(4.0f));
  CHECK(f.node(f.second, "Set/A").get<world::PointLight>().intensity == Approx(4.0f));
  CHECK(f.node(f.first, "Set/B").get<world::PointLight>().intensity == Approx(10.0f));
  CHECK(f.node(f.second, "Set/B").get<world::PointLight>().intensity == Approx(99.0f));
  // The overridden node is the same entity identity as before the insert.
  CHECK(f.world.uuidOf(f.node(f.second, "Set/B")) == bId);
  // Added node on both, removed node on neither.
  REQUIRE(f.node(f.first, "Set/New"));
  REQUIRE(f.node(f.second, "Set/New"));
  CHECK(f.node(f.second, "Set/New").get<world::PointLight>().intensity == Approx(7.0f));
  CHECK(!f.node(f.first, "Set/C"));
  CHECK(!f.node(f.second, "Set/C"));
  // The new copy has the identity a fresh instantiate of the new prefab would give.
  const flecs::entity fresh = f.world.instantiate(f.prefab, "Fresh");
  CHECK(f.world.uuidOf(f.node(fresh, "Set/New")) != core::Uuid{});
  CHECK(f.world.uuidOf(f.node(f.first, "Set/New")) ==
        world::World::instancedIdentity(firstId, pathIdentity(f.world.uuidOf(f.prefab), "Set/New")));

  // A save writes only what the scene overrides, and loads back to the same values.
  const nlohmann::json saved = world::saveScene(f.world);
  std::size_t instanced = 0;
  for (const auto &entry : saved["entities"]) {
    instanced += entry.value("instanced", false) ? 1U : 0U;
  }
  CHECK(instanced == 1);
}

TEST_CASE("spike: with index identities an inserted node moves an override to the wrong node", "[spike]") {
  const core::Uuid uuid = core::Uuid::generate();
  world::World world;
  const assets::Model before = modelOf({{"Set", -1, 0.0f}, {"A", 0, 10.0f}, {"B", 0, 10.0f}});
  flecs::entity prefab = world::loadModelPrefab(world, before, uuid, "Model");
  const flecs::entity instance = world.instantiate(prefab, "I");
  world.findByPath(instance, "Set/B").set<world::PointLight>({.color = {1, 1, 1}, .intensity = 99.0f, .range = 5.0f, .castsShadows = false});
  const nlohmann::json scene = world::saveScene(world);
  world.clearScene();
  world.destroyEntity(prefab);
  const assets::Model after = modelOf({{"Set", -1, 0.0f}, {"New", 0, 3.0f}, {"A", 0, 10.0f}, {"B", 0, 10.0f}});
  prefab = world::loadModelPrefab(world, after, uuid, "Model");
  REQUIRE(world::loadScene(world, scene).has_value());
  const flecs::entity reloaded = world.roots().front();
  // The override was stored on node 2 of the old model; node 2 is now "A".
  CHECK(world.findByPath(reloaded, "Set/A").get<world::PointLight>().intensity == Approx(99.0f));
  CHECK(world.findByPath(reloaded, "Set/B").get<world::PointLight>().intensity == Approx(10.0f));
}

TEST_CASE("spike: duplicate sibling names take the occurrence order", "[spike]") {
  const assets::Model before = modelOf({{"Rig", -1, 0.0f}, {"Bone", 0, 1.0f}, {"Bone", 0, 2.0f}});
  Fixture f{before};
  const core::Uuid firstId = f.world.uuidOf(f.first);
  // Override the second Bone (children in sibling order).
  const flecs::entity rig = f.world.children(f.first).front();
  const std::vector<flecs::entity> bones = f.world.children(rig);
  bones[1].set<world::PointLight>({.color = {1, 1, 1}, .intensity = 50.0f, .range = 5.0f, .castsShadows = false});
  refreshByMerge(f.world, f.prefab, modelOf({{"Rig", -1, 0.0f}, {"Bone", 0, 1.5f}, {"Bone", 0, 2.0f}, {"Bone", 0, 3.0f}}));
  const std::vector<flecs::entity> after = f.world.children(f.world.children(f.world.find(firstId)).front());
  REQUIRE(after.size() == 3);
  CHECK(after[0].get<world::PointLight>().intensity == Approx(1.5f));
  CHECK(after[1].get<world::PointLight>().intensity == Approx(50.0f));
  CHECK(after[2].get<world::PointLight>().intensity == Approx(3.0f));
}

TEST_CASE("spike: where an inserted node lands among its siblings, and what a removed node takes with it", "[spike]") {
  const assets::Model before = modelOf({{"Set", -1, 0.0f}, {"A", 0, 1.0f}, {"B", 0, 1.0f}});
  Fixture f{before};
  const core::Uuid firstId = f.world.uuidOf(f.first);
  // A child the scene added under B.
  const flecs::entity b = f.node(f.first, "Set/B");
  const flecs::entity extra = f.world.createEntity("Extra", b, core::Uuid::generate());
  const core::Uuid extraId = f.world.uuidOf(extra);
  refreshByMerge(f.world, f.prefab, modelOf({{"Set", -1, 0.0f}, {"A", 0, 1.0f}, {"New", 0, 1.0f}, {"B", 0, 1.0f}}));
  const std::vector<flecs::entity> order = f.world.children(f.world.children(f.world.find(firstId)).front());
  std::string names;
  for (const flecs::entity e : order) {
    names += e.get<world::Name>().value + ",";
  }
  INFO("copy order: " << names);
  const std::vector<flecs::entity> prefabOrder = f.world.children(f.world.children(f.prefab).front());
  std::string prefabNames;
  for (const flecs::entity e : prefabOrder) {
    prefabNames += e.get<world::Name>().value + ",";
  }
  INFO("prefab order: " << prefabNames);
  CHECK(names == prefabNames);
  // Remove B: the scene's child goes with it.
  refreshByMerge(f.world, f.prefab, modelOf({{"Set", -1, 0.0f}, {"A", 0, 1.0f}, {"New", 0, 1.0f}}));
  CHECK(!f.world.find(extraId));
}

TEST_CASE("spike: cost of the reload and the merge at scale", "[spike][.cost]") {
  const assets::Model before = modelOf({{"Set", -1, 0.0f}, {"A", 0, 1.0f}, {"B", 0, 1.0f}, {"C", 0, 1.0f}, {"D", 0, 1.0f}});
  const assets::Model after = modelOf({{"Set", -1, 0.0f}, {"A", 0, 2.0f}, {"New", 0, 1.0f}, {"B", 0, 1.0f}, {"C", 0, 1.0f}, {"D", 0, 1.0f}});
  for (const int count : {500, 2000}) {
    for (const How how : {How::Reload, How::Merge}) {
      Fixture f{before};
      for (int i = 0; i < count; ++i) {
        static_cast<void>(f.world.instantiate(f.prefab, "I"));
      }
      const auto start = std::chrono::steady_clock::now();
      if (how == How::Reload) {
        f.prefab = refreshByReload(f.world, f.prefab, after);
      } else {
        refreshByMerge(f.world, f.prefab, after);
      }
      const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
      WARN((how == How::Reload ? "reload " : "merge ") << count << " instances x 6 nodes: " << ms << " ms");
    }
  }
}

TEST_CASE("spike: a scene saved before a re-export loads onto a rebuilt prefab in a fresh world", "[spike]") {
  const core::Uuid uuid = core::Uuid::generate();
  nlohmann::json scene;
  core::Uuid instanceId;
  {
    world::World world;
    const flecs::entity prefab = buildPrefab(world, modelOf({{"Set", -1, 0.0f}, {"A", 0, 10.0f}, {"B", 0, 10.0f}}), uuid);
    const flecs::entity instance = world.instantiate(prefab, "I");
    instanceId = world.uuidOf(instance);
    world.findByPath(instance, "Set/B").set<world::PointLight>({.color = {1, 1, 1}, .intensity = 99.0f, .range = 5.0f, .castsShadows = false});
    scene = world::saveScene(world);
  }
  // The project reopens after the model was re-exported with a node inserted ahead of B.
  world::World reopened;
  static_cast<void>(buildPrefab(reopened, modelOf({{"Set", -1, 0.0f}, {"New", 0, 3.0f}, {"A", 0, 10.0f}, {"B", 0, 10.0f}}), uuid));
  REQUIRE(world::loadScene(reopened, scene).has_value());
  const flecs::entity instance = reopened.find(instanceId);
  REQUIRE(instance);
  CHECK(reopened.findByPath(instance, "Set/A").get<world::PointLight>().intensity == Approx(10.0f));
  CHECK(reopened.findByPath(instance, "Set/B").get<world::PointLight>().intensity == Approx(99.0f));
  CHECK(reopened.findByPath(instance, "Set/New").get<world::PointLight>().intensity == Approx(3.0f));
}

TEST_CASE("spike: a scene saved under index identities loads onto name-path ones, dropping the old overrides", "[spike]") {
  const core::Uuid uuid = core::Uuid::generate();
  const assets::Model model = modelOf({{"Set", -1, 0.0f}, {"A", 0, 10.0f}, {"B", 0, 10.0f}});
  nlohmann::json scene;
  core::Uuid instanceId;
  {
    world::World world;
    const flecs::entity prefab = world::loadModelPrefab(world, model, uuid, "Model");
    const flecs::entity instance = world.instantiate(prefab, "I");
    instanceId = world.uuidOf(instance);
    world.findByPath(instance, "Set/B").set<world::PointLight>({.color = {1, 1, 1}, .intensity = 99.0f, .range = 5.0f, .castsShadows = false});
    scene = world::saveScene(world);
  }
  world::World reopened;
  static_cast<void>(buildPrefab(reopened, model, uuid));
  REQUIRE(world::loadScene(reopened, scene).has_value()); // a warning, not an error
  const flecs::entity instance = reopened.find(instanceId);
  REQUIRE(instance);
  CHECK(reopened.findByPath(instance, "Set/B").get<world::PointLight>().intensity == Approx(10.0f)); // override dropped, nothing misplaced
}
