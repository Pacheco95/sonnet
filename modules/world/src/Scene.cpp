#include <sonnet/world/Scene.h>

#include <sonnet/assets/Json.h>
#include <sonnet/core/Assert.h>
#include <sonnet/core/File.h>
#include <sonnet/core/Log.h>

#include <array>
#include <format>
#include <fstream>
#include <utility>

namespace sonnet::world {

namespace {

using nlohmann::json;

bool isSceneEntity(flecs::entity entity) {
  return entity.has<Identity>() && !entity.has(flecs::Prefab) && !entity.has<EditorOnly>();
}

json saveEntity(const World &world, flecs::entity entity, flecs::entity parent) {
  json entry;
  entry["uuid"] = world.uuidOf(entity).toString();
  const Name *name = entity.try_get<Name>();
  entry["name"] = name != nullptr ? name->value : std::string{};
  if (parent) {
    entry["parent"] = world.uuidOf(parent).toString();
  }
  const bool instance = world.isInstance(entity);
  if (instance) {
    entry["prefab"] = world.uuidOf(world.prefabOf(entity)).toString();
  }
  json components = json::object();
  for (const ComponentInfo &component : world.components()) {
    if (!entity.has(component.id) || (instance && !entity.owns(component.id))) {
      continue; // an instance stores only what it overrides
    }
    components[component.name] = component.tag ? json{} : world.componentToJson(entity, component.id);
  }
  entry["components"] = std::move(components);
  return entry;
}

void saveSubtree(const World &world, flecs::entity entity, flecs::entity parent, json &entities, bool prefab) {
  if (!prefab && !isSceneEntity(entity)) {
    return;
  }
  entities.push_back(saveEntity(world, entity, parent));
  if (world.isInstance(entity)) {
    return; // instantiated children come back with the prefab
  }
  for (const flecs::entity child : world.children(entity)) {
    saveSubtree(world, child, entity, entities, prefab);
  }
}

// Version 1 named one of the renderer's primitives; version 2 references the built-in mesh of
// the same shape by identity.
void migrateVersion1(json &document) {
  static const std::array<std::pair<const char *, core::Uuid>, 5> Primitives{{{"Box", assets::builtin::box()},
                                                                              {"Sphere", assets::builtin::sphere()},
                                                                              {"Plane", assets::builtin::plane()},
                                                                              {"Cylinder", assets::builtin::cylinder()},
                                                                              {"Capsule", assets::builtin::capsule()}}};
  if (!document.contains("entities") || !document["entities"].is_array()) {
    return;
  }
  for (json &entry : document["entities"]) {
    if (!entry.is_object() || !entry.contains("components") || !entry["components"].is_object()) {
      continue;
    }
    json &components = entry["components"];
    if (!components.contains("MeshRenderer") || !components["MeshRenderer"].is_object()) {
      continue;
    }
    json &meshRenderer = components["MeshRenderer"];
    const std::string primitive = meshRenderer.value("primitive", std::string{"Box"});
    meshRenderer.erase("primitive");
    core::Uuid mesh = assets::builtin::box();
    for (const auto &[name, uuid] : Primitives) {
      if (primitive == name) {
        mesh = uuid;
      }
    }
    meshRenderer["mesh"] = mesh.toString();
  }
}

// Checks the version and brings an older document up to date, oldest step first, logging the
// source and both versions (docs/conventions.md, "Logging").
core::Result<json> migrate(const json &input, std::string_view source) {
  if (!input.is_object() || !input.contains("version") || !input["version"].is_number_integer()) {
    return std::unexpected(core::Error{"not a scene: no version field", core::ErrorCategory::Io});
  }
  const int version = input["version"].get<int>();
  if (version > SceneVersion) {
    return std::unexpected(
        core::Error{std::format("scene version {} is newer than this engine's {}", version, SceneVersion),
                    core::ErrorCategory::Io});
  }
  if (version < 1) {
    return std::unexpected(core::Error{std::format("scene version {} is not valid", version), core::ErrorCategory::Io});
  }
  json document = input;
  if (version < 2) {
    migrateVersion1(document);
    SONNET_LOG_INFO("{}: migrated scene version 1 to 2", source);
  }
  document["version"] = SceneVersion;
  return document;
}

core::Result<std::vector<flecs::entity>> loadEntities(World &world, const json &input, bool asPrefab,
                                                      std::string_view source) {
  auto migrated = migrate(input, source);
  if (!migrated) {
    return std::unexpected(migrated.error());
  }
  const json &document = *migrated;
  if (!document.contains("entities") || !document["entities"].is_array()) {
    return std::unexpected(core::Error{"scene has no entities array", core::ErrorCategory::Io});
  }
  std::vector<flecs::entity> loaded;
  struct Pending {
    flecs::entity entity;
    core::Uuid parent;
  };
  std::vector<Pending> pending;
  const auto fail = [&](std::string message) -> core::Result<std::vector<flecs::entity>> {
    for (const flecs::entity entity : loaded) {
      world.destroyEntity(entity);
    }
    return std::unexpected(core::Error{std::move(message), core::ErrorCategory::Io});
  };

  for (const json &entry : document["entities"]) {
    const auto uuid = entry.contains("uuid") && entry["uuid"].is_string()
                          ? core::Uuid::parse(entry["uuid"].get_ref<const std::string &>())
                          : std::nullopt;
    if (!uuid) {
      return fail(std::format("entity {} has no valid uuid", loaded.size()));
    }
    if (world.find(*uuid)) {
      return fail(std::format("entity {} is already in the world", uuid->toString()));
    }
    const std::string name = entry.value("name", std::string{});
    flecs::entity entity;
    if (entry.contains("prefab")) {
      const auto prefabUuid = core::Uuid::parse(entry["prefab"].get<std::string>());
      const flecs::entity prefab = prefabUuid ? world.find(*prefabUuid) : flecs::entity{};
      if (!prefab || !prefab.has(flecs::Prefab)) {
        return fail(std::format("entity \"{}\" refers to prefab {}, which is not loaded", name,
                                entry["prefab"].get<std::string>()));
      }
      entity = world.instantiate(prefab, name);
      entity.set<Identity>({*uuid});
    } else {
      entity = world.createEntity(name, {}, *uuid);
    }
    if (asPrefab) {
      entity.add(flecs::Prefab);
    }
    loaded.push_back(entity);
    if (entry.contains("components") && entry["components"].is_object()) {
      for (const auto &[componentName, value] : entry["components"].items()) {
        const ComponentInfo *info = world.findComponent(componentName);
        if (info == nullptr) {
          SONNET_LOG_WARN("entity \"{}\": unknown component \"{}\" ignored", name, componentName);
          continue;
        }
        world.componentFromJson(entity, info->id, value);
      }
    }
    if (entry.contains("parent") && entry["parent"].is_string()) {
      const auto parentUuid = core::Uuid::parse(entry["parent"].get_ref<const std::string &>());
      if (!parentUuid) {
        return fail(std::format("entity \"{}\" has an invalid parent uuid", name));
      }
      pending.push_back({entity, *parentUuid});
    }
  }
  // Parents are resolved after every entity exists, whatever the file's order.
  for (const Pending &item : pending) {
    const flecs::entity parent = world.find(item.parent);
    if (!parent) {
      return fail(std::format("entity \"{}\" refers to parent {}, which does not exist",
                              item.entity.try_get<Name>() != nullptr ? item.entity.try_get<Name>()->value : "",
                              item.parent.toString()));
    }
    item.entity.child_of(parent);
  }
  return loaded;
}

core::Result<json> readJson(const std::filesystem::path &path) {
  const auto bytes = core::readFile(path);
  if (!bytes) {
    return std::unexpected(bytes.error());
  }
  json document = assets::parseJson(*bytes);
  if (document.is_discarded()) {
    return std::unexpected(core::Error{std::format("{}: not valid JSON", path.string()), core::ErrorCategory::Io});
  }
  return document;
}

core::Result<void> writeJson(const json &document, const std::filesystem::path &path) {
  std::ofstream file{path};
  if (!file) {
    return std::unexpected(
        core::Error{std::format("{}: cannot open for writing", path.string()), core::ErrorCategory::Io});
  }
  file << document.dump(2) << '\n';
  if (!file) {
    return std::unexpected(core::Error{std::format("{}: write failed", path.string()), core::ErrorCategory::Io});
  }
  return {};
}

} // namespace

core::Result<flecs::entity> loadPrefabFrom(World &world, const json &prefab, std::string_view source);

json saveScene(const World &world) {
  json scene;
  scene["version"] = SceneVersion;
  json entities = json::array();
  for (const flecs::entity root : world.roots()) {
    saveSubtree(world, root, {}, entities, false);
  }
  scene["entities"] = std::move(entities);
  return scene;
}

core::Result<std::vector<flecs::entity>> loadScene(World &world, const json &scene) {
  return loadEntities(world, scene, false, "scene");
}

core::Result<void> saveSceneFile(const World &world, const std::filesystem::path &path) {
  return writeJson(saveScene(world), path);
}

core::Result<std::vector<flecs::entity>> loadSceneFile(World &world, const std::filesystem::path &path) {
  const auto document = readJson(path);
  if (!document) {
    return std::unexpected(document.error());
  }
  auto loaded = loadEntities(world, *document, false, path.string());
  if (!loaded) {
    return std::unexpected(core::Error{std::format("{}: {}", path.string(), loaded.error().message),
                                       core::ErrorCategory::Io, loaded.error().location});
  }
  SONNET_LOG_INFO("loaded {} entities from {}", loaded->size(), path.string());
  return loaded;
}

json saveSubtree(const World &world, flecs::entity root) {
  json document;
  document["version"] = SceneVersion;
  json entities = json::array();
  saveSubtree(world, root, {}, entities, true);
  document["entities"] = std::move(entities);
  return document;
}

json savePrefab(const World &world, flecs::entity root) {
  return saveSubtree(world, root);
}

core::Result<flecs::entity> loadPrefab(World &world, const json &prefab) {
  return loadPrefabFrom(world, prefab, "prefab");
}

core::Result<flecs::entity> loadPrefabFrom(World &world, const json &prefab, std::string_view source) {
  auto loaded = loadEntities(world, prefab, true, source);
  if (!loaded) {
    return std::unexpected(loaded.error());
  }
  flecs::entity root;
  for (const flecs::entity entity : *loaded) {
    if (!entity.parent()) {
      if (root) {
        for (const flecs::entity created : *loaded) {
          world.destroyEntity(created);
        }
        return std::unexpected(core::Error{"a prefab has exactly one root entity", core::ErrorCategory::Io});
      }
      root = entity;
    }
  }
  if (!root) {
    return std::unexpected(core::Error{"a prefab has exactly one root entity", core::ErrorCategory::Io});
  }
  return root;
}

core::Result<void> savePrefabFile(const World &world, flecs::entity root, const std::filesystem::path &path) {
  return writeJson(savePrefab(world, root), path);
}

core::Result<flecs::entity> loadPrefabFile(World &world, const std::filesystem::path &path) {
  const auto document = readJson(path);
  if (!document) {
    return std::unexpected(document.error());
  }
  auto root = loadPrefabFrom(world, *document, path.string());
  if (!root) {
    return std::unexpected(core::Error{std::format("{}: {}", path.string(), root.error().message),
                                       core::ErrorCategory::Io, root.error().location});
  }
  SONNET_LOG_INFO("loaded prefab {} from {}", world.uuidOf(*root).toString(), path.string());
  return root;
}

flecs::entity loadModelPrefab(World &world, const assets::Model &model, const core::Uuid &uuid, std::string_view name) {
  SONNET_ASSERT(!world.find(uuid), "model prefab {} is already loaded", uuid.toString());
  flecs::entity root = world.createEntity(name, {}, uuid);
  root.add(flecs::Prefab);
  std::vector<flecs::entity> entities;
  entities.reserve(model.nodes.size());
  for (std::size_t n = 0; n < model.nodes.size(); ++n) {
    const assets::ModelNode &node = model.nodes[n];
    const flecs::entity parent = node.parent >= 0 ? entities[static_cast<std::size_t>(node.parent)] : root;
    flecs::entity entity = world.createEntity(node.name, parent, core::Uuid::derive(uuid, std::format("node/{}", n)));
    entity.add(flecs::Prefab);
    entity.set<Transform>({.position = node.position, .rotation = node.rotation, .scale = node.scale});
    if (!node.mesh.isNil()) {
      entity.set<MeshRenderer>({.mesh = node.mesh, .material = {}, .color = {1.0f, 1.0f, 1.0f, 1.0f}, .visible = true});
    }
    if (!node.skin.isNil()) {
      entity.set<SkinnedMesh>({node.skin});
    }
    entities.push_back(entity);
  }
  // A model with clips plays its first on the root, where the clips' paths start.
  if (!model.animations.empty()) {
    root.set<Animator>({.clip = model.animations.front(), .time = 0.0f, .speed = 1.0f, .playing = true, .loop = true});
  }
  return root;
}

} // namespace sonnet::world
