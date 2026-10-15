// SPIKE (#304, #305): throwaway. Prototypes refreshing a model prefab and its instances after a
// re-import, two ways, with node identities derived from name paths.
#include <sonnet/world/Scene.h>
#include <sonnet/world/World.h>

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

struct Spec {
  std::string name;
  int parent;
  float intensity; // a point light when > 0
  float x{0.0f};
};

assets::Model modelOf(const std::vector<Spec> &specs) {
  assets::Model model;
  for (const Spec &spec : specs) {
    assets::ModelNode node{.name = spec.name, .parent = spec.parent, .position = {spec.x, 0.0f, 0.0f}};
    if (spec.intensity > 0.0f) {
      node.light = assets::ModelLight{
          .type = assets::ModelLight::Type::Point, .color = {1.0f, 1.0f, 1.0f}, .intensity = spec.intensity, .range = 5.0f};
    }
    model.nodes.push_back(node);
  }
  return model;
}

// A node's key among its siblings: its name, and the count of equal names before it.
std::string keyOf(const std::vector<std::string> &before, const std::string &name) {
  std::size_t count = 0;
  for (const std::string &other : before) {
    count += other == name ? 1U : 0U;
  }
  return count == 0 ? name : name + "#" + std::to_string(count);
}

core::Uuid pathIdentity(const core::Uuid &base, const std::string &path) {
  return core::Uuid::derive(base, "path/" + path);
}

void setNode(world::World &world, flecs::entity entity, const assets::ModelNode &node) {
  entity.set<world::Transform>({.position = node.position, .rotation = node.rotation, .scale = node.scale});
  if (node.light) {
    entity.set<world::PointLight>({.color = node.light->color,
                                   .intensity = node.light->intensity,
                                   .range = node.light->range,
                                   .castsShadows = false});
  }
  static_cast<void>(world);
}

// loadModelPrefab with identities from name paths.
flecs::entity buildPrefab(world::World &world, const assets::Model &model, const core::Uuid &uuid) {
  flecs::entity root = world.createEntity("Model", {}, uuid);
  std::vector<flecs::entity> entities;
  std::vector<std::string> paths;
  std::map<std::int32_t, std::vector<std::string>> seen;
  for (const assets::ModelNode &node : model.nodes) {
    const flecs::entity parent = node.parent >= 0 ? entities[static_cast<std::size_t>(node.parent)] : root;
    const std::string key = keyOf(seen[node.parent], node.name);
    seen[node.parent].push_back(node.name);
    const std::string path = node.parent >= 0 ? paths[static_cast<std::size_t>(node.parent)] + "/" + key : key;
    flecs::entity entity = world.createEntity(node.name, parent, pathIdentity(uuid, path));
    setNode(world, entity, node);
    entities.push_back(entity);
    paths.push_back(path);
  }
  world.makePrefab(root);
  return root;
}

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

// ---- Approach M: three-way merge in place. ----
std::vector<std::pair<std::string, flecs::entity>> keyed(const world::World &world, flecs::entity node) {
  std::vector<std::pair<std::string, flecs::entity>> result;
  std::vector<std::string> names;
  for (const flecs::entity child : world.children(node)) {
    const std::string &name = child.get<world::Name>().value;
    result.emplace_back(keyOf(names, name), child);
    names.push_back(name);
  }
  return result;
}

std::vector<flecs::entity> copiesOf(world::World &world, flecs::entity source) {
  std::vector<flecs::entity> copies;
  const core::Uuid id = world.uuidOf(source);
  world.ecs().query_builder<const world::PrefabSource>().build().each(
      [&](flecs::entity entity, const world::PrefabSource &from) {
        if (from.uuid == id) {
          copies.push_back(entity);
        }
      });
  return copies;
}

std::vector<flecs::entity> instanceRoots(world::World &world, flecs::entity prefab) {
  std::vector<flecs::entity> roots;
  world.ecs().query_builder().with(flecs::IsA, prefab).build().each([&](flecs::entity entity) {
    if (!entity.has<world::PrefabSource>()) {
      roots.push_back(entity);
    }
  });
  return roots;
}

void copyComponents(world::World &world, flecs::entity from, flecs::entity to) {
  for (const world::ComponentInfo &component : world.components()) {
    if (from.has(component.id) && component.name != std::string{"Name"}) {
      if (component.tag) {
        to.add(component.id);
      } else {
        world.componentFromJson(to, component.id, world.componentToJson(from, component.id));
      }
    }
  }
}

void mergeComponents(world::World &world, flecs::entity real, flecs::entity next,
                     const std::vector<flecs::entity> &copies) {
  for (const world::ComponentInfo &component : world.components()) {
    if (component.name == std::string{"Name"}) {
      continue;
    }
    const bool hadIt = real.has(component.id);
    const bool hasIt = next.has(component.id);
    if (!hadIt && !hasIt) {
      continue;
    }
    if (hadIt && hasIt && (component.tag || world.componentToJson(real, component.id) == world.componentToJson(next, component.id))) {
      continue;
    }
    for (const flecs::entity copy : copies) {
      if (hadIt && !copy.has(component.id)) {
        continue; // the scene removed it
      }
      if (hadIt && world.differs(copy, real, component)) {
        continue; // the scene overrides it
      }
      if (hasIt) {
        world.componentFromJson(copy, component.id, component.tag ? nlohmann::json{} : world.componentToJson(next, component.id));
      } else {
        copy.remove(component.id);
      }
    }
    if (hasIt) {
      world.componentFromJson(real, component.id, component.tag ? nlohmann::json{} : world.componentToJson(next, component.id));
    } else {
      real.remove(component.id);
    }
  }
}

void addNode(world::World &world, flecs::entity realParent, flecs::entity next, const std::string &path,
             const core::Uuid &base) {
  const core::Uuid id = pathIdentity(base, path);
  const std::string &name = next.get<world::Name>().value;
  flecs::entity real = world.createEntity(name, realParent, id);
  copyComponents(world, next, real);
  real.add(flecs::Prefab);
  real.set<world::PrefabOrigin>({id});
  std::vector<flecs::entity> parents = copiesOf(world, realParent);
  if (!realParent.parent() && realParent.has(flecs::Prefab)) {
    parents = instanceRoots(world, realParent);
  }
  for (const flecs::entity parent : parents) {
    const flecs::entity owner = parent.has<world::PrefabSource>() ? world.instanceOf(parent) : parent;
    flecs::entity copy = world.createEntity(name, parent, world::World::instancedIdentity(world.uuidOf(owner), id));
    copyComponents(world, next, copy);
    copy.set<world::PrefabSource>({id});
  }
  for (const auto &[key, child] : keyed(world, next)) {
    addNode(world, real, child, path + "/" + key, base);
  }
}

void mergeChildren(world::World &world, flecs::entity real, flecs::entity next, const std::string &path,
                   const core::Uuid &base) {
  const auto realKids = keyed(world, real);
  const auto nextKids = keyed(world, next);
  std::map<std::string, flecs::entity> byKey(realKids.begin(), realKids.end());
  std::map<std::string, flecs::entity> nextByKey(nextKids.begin(), nextKids.end());
  for (const auto &[key, child] : nextKids) {
    const std::string childPath = path.empty() ? key : path + "/" + key;
    if (const auto found = byKey.find(key); found != byKey.end()) {
      mergeComponents(world, found->second, child, copiesOf(world, found->second));
      mergeChildren(world, found->second, child, childPath, base);
    } else {
      addNode(world, real, child, childPath, base);
    }
  }
  for (const auto &[key, child] : realKids) {
    if (!nextByKey.contains(key)) {
      for (const flecs::entity copy : copiesOf(world, child)) {
        world.destroyEntity(copy);
      }
      world.destroyEntity(child);
    }
  }
}

void refreshByMerge(world::World &world, flecs::entity prefab, const assets::Model &next) {
  const core::Uuid base = world.uuidOf(prefab);
  const flecs::entity scratch = buildPrefab(world, next, core::Uuid::generate());
  mergeChildren(world, prefab, scratch, "", base);
  world.destroyEntity(scratch);
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
