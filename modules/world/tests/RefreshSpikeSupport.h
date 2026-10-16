// SPIKE (#304, #305): throwaway helpers shared by RefreshSpike.cpp and NestedRefreshSpike.cpp.
#pragma once
#include <sonnet/world/Scene.h>
#include <sonnet/world/World.h>

#include <map>
#include <string>
#include <vector>

namespace sonnet::spike {

struct Spec {
  std::string name;
  int parent;
  float intensity; // a point light when > 0
  float x{0.0f};
};

inline assets::Model modelOf(const std::vector<Spec> &specs) {
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
inline std::string keyOf(const std::vector<std::string> &before, const std::string &name) {
  std::size_t count = 0;
  for (const std::string &other : before) {
    count += other == name ? 1U : 0U;
  }
  return count == 0 ? name : name + "#" + std::to_string(count);
}

inline core::Uuid pathIdentity(const core::Uuid &base, const std::string &path) {
  return core::Uuid::derive(base, "path/" + path);
}

inline void setNode(world::World &world, flecs::entity entity, const assets::ModelNode &node) {
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
inline flecs::entity buildPrefab(world::World &world, const assets::Model &model, const core::Uuid &uuid) {
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

// ---- Approach M: three-way merge in place. ----
inline std::vector<std::pair<std::string, flecs::entity>> keyed(const world::World &world, flecs::entity node) {
  std::vector<std::pair<std::string, flecs::entity>> result;
  std::vector<std::string> names;
  for (const flecs::entity child : world.children(node)) {
    const std::string &name = child.get<world::Name>().value;
    result.emplace_back(keyOf(names, name), child);
    names.push_back(name);
  }
  return result;
}

inline std::vector<flecs::entity> copiesOf(world::World &world, flecs::entity source) {
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

inline std::vector<flecs::entity> instanceRoots(world::World &world, flecs::entity prefab) {
  std::vector<flecs::entity> roots;
  world.ecs().query_builder().with(flecs::IsA, prefab).build().each([&](flecs::entity entity) {
    if (!entity.has<world::PrefabSource>()) {
      roots.push_back(entity);
    }
  });
  return roots;
}

inline void copyComponents(world::World &world, flecs::entity from, flecs::entity to) {
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

inline void mergeComponents(world::World &world, flecs::entity real, flecs::entity next,
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

inline void addNode(world::World &world, flecs::entity realParent, flecs::entity next, const std::string &path,
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

inline void mergeChildren(world::World &world, flecs::entity real, flecs::entity next, const std::string &path,
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

inline void refreshByMerge(world::World &world, flecs::entity prefab, const assets::Model &next) {
  const core::Uuid base = world.uuidOf(prefab);
  const flecs::entity scratch = buildPrefab(world, next, core::Uuid::generate());
  mergeChildren(world, prefab, scratch, "", base);
  world.destroyEntity(scratch);
}

} // namespace sonnet::spike
