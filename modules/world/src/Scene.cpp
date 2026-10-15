#include <sonnet/world/Scene.h>

#include <sonnet/assets/Json.h>
#include <sonnet/core/Assert.h>
#include <sonnet/core/File.h>
#include <sonnet/core/Log.h>

#include <algorithm>
#include <array>
#include <format>
#include <fstream>
#include <unordered_map>
#include <utility>

namespace sonnet::world {

namespace {

using nlohmann::json;

// A string member, or `fallback` when the member is missing or not a string. Read through
// get_ref: GCC 14 at -O3 flags nlohmann's value<std::string> with a false null dereference.
std::string stringOr(const json &object, const char *key, std::string_view fallback) {
  const auto it = object.find(key);
  return it != object.end() && it->is_string() ? it->get_ref<const std::string &>() : std::string{fallback};
}

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

void saveSubtree(const World &world, flecs::entity entity, flecs::entity parent, json &entities, bool prefab);

// An instance's descendant as an "instanced" entry: what differs from the prefab entity it was
// copied from, compared by value since flecs copied the whole entity (ADR-0028). Null when
// nothing does.
json saveInstanced(const World &world, flecs::entity entity, flecs::entity source) {
  json components = json::object();
  json removed = json::array();
  for (const ComponentInfo &component : world.components()) {
    if (world.differs(entity, source, component)) {
      components[component.name] = component.tag ? json{} : world.componentToJson(entity, component.id);
    } else if (!entity.has(component.id) && source.has(component.id)) {
      removed.push_back(component.name);
    }
  }
  const std::string &name = entity.get<Name>().value;
  const bool renamed = name != source.get<Name>().value;
  if (components.empty() && removed.empty() && !renamed) {
    return nullptr;
  }
  json entry;
  entry["uuid"] = world.uuidOf(entity).toString();
  entry["instanced"] = true;
  if (renamed) {
    entry["name"] = name;
  }
  entry["components"] = std::move(components);
  if (!removed.empty()) {
    entry["removed"] = std::move(removed);
  }
  return entry;
}

// The descendants of an instance, at every depth: the copies as what they changed, the children
// added under them whole.
void saveInstanceChildren(const World &world, flecs::entity node, json &entities, bool prefab) {
  for (const flecs::entity child : world.children(node)) {
    if (!child.has<PrefabSource>()) {
      saveSubtree(world, child, node, entities, prefab);
      continue;
    }
    if (const flecs::entity source = world.sourceOf(child)) {
      if (json entry = saveInstanced(world, child, source); !entry.is_null()) {
        entities.push_back(std::move(entry));
      }
    }
    saveInstanceChildren(world, child, entities, prefab);
  }
}

void saveSubtree(const World &world, flecs::entity entity, flecs::entity parent, json &entities, bool prefab) {
  if (!prefab && !isSceneEntity(entity)) {
    return;
  }
  entities.push_back(saveEntity(world, entity, parent));
  if (world.isInstance(entity)) {
    saveInstanceChildren(world, entity, entities, prefab);
    return;
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
    const std::string primitive = stringOr(meshRenderer, "primitive", "Box");
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

// Version 3 holds several scripts per entity: each `Script` becomes a one-slot `Scripts` with
// the same asset and every property at its default (ADR-0022).
void migrateVersion2(json &document) {
  if (!document.contains("entities") || !document["entities"].is_array()) {
    return;
  }
  for (json &entry : document["entities"]) {
    if (!entry.is_object() || !entry.contains("components") || !entry["components"].is_object()) {
      continue;
    }
    json &components = entry["components"];
    if (!components.contains("Script")) {
      continue;
    }
    const json script = components["Script"].is_object() ? components["Script"].value("script", json{}) : json{};
    json slot = {{"properties", json::object()}};
    if (script.is_string()) {
      slot["script"] = script;
    }
    components.erase("Script");
    components["Scripts"] = {{"slots", json::array({std::move(slot)})}};
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
  if (version < 3) {
    migrateVersion2(document);
    SONNET_LOG_INFO("{}: migrated scene version 2 to 3", source);
  }
  // Version 4 adds "instanced" entries, which a version 3 file does not have: nothing to change.
  // Version 5 writes vectors as arrays. Only reflection knows which members are vectors, so the
  // component reader accepts the older objects instead (setComponents, ADR-0029).
  document["version"] = SceneVersion;
  return document;
}

// `version` is the file's own, before migration: one older than 5 holds vectors as objects.
void setComponents(World &world, flecs::entity entity, const json &entry, const core::Uuid &uuid, int version) {
  if (!entry.contains("components") || !entry["components"].is_object()) {
    return;
  }
  for (const auto &[componentName, value] : entry["components"].items()) {
    const ComponentInfo *info = world.findComponent(componentName);
    if (info == nullptr) {
      SONNET_LOG_WARN("entity {}: unknown component \"{}\" ignored", uuid.toString(), componentName);
      continue;
    }
    world.componentFromJson(entity, info->id, value, version < 5);
  }
}

core::Result<std::vector<flecs::entity>> loadEntities(World &world, const json &input, std::string_view source) {
  auto migrated = migrate(input, source);
  if (!migrated) {
    return std::unexpected(migrated.error());
  }
  const json &document = *migrated;
  const int version = input["version"].get<int>();
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
    if (entry.value("instanced", false)) {
      // What an instance's descendant changed; the descendant came with the instance, which the
      // file lists before it.
      const flecs::entity target = world.find(*uuid);
      if (!target || !target.has<PrefabSource>()) {
        SONNET_LOG_WARN("{}: changes to {} skipped, its prefab no longer has the entity", source, uuid->toString());
        continue;
      }
      if (entry.contains("name") && entry["name"].is_string()) {
        target.set<Name>({entry["name"].get<std::string>()});
      }
      setComponents(world, target, entry, *uuid, version);
      if (entry.contains("removed") && entry["removed"].is_array()) {
        for (const json &componentName : entry["removed"]) {
          const ComponentInfo *info =
              componentName.is_string() ? world.findComponent(componentName.get<std::string>()) : nullptr;
          if (info != nullptr) {
            target.remove(info->id);
          }
        }
      }
      continue;
    }
    if (world.find(*uuid)) {
      return fail(std::format("entity {} is already in the world", uuid->toString()));
    }
    const std::string name = stringOr(entry, "name", {});
    flecs::entity entity;
    if (entry.contains("prefab")) {
      const auto prefabUuid = core::Uuid::parse(entry["prefab"].get<std::string>());
      const flecs::entity prefab = prefabUuid ? world.find(*prefabUuid) : flecs::entity{};
      if (!prefab || !prefab.has(flecs::Prefab)) {
        return fail(std::format("entity \"{}\" refers to prefab {}, which is not loaded", name,
                                entry["prefab"].get<std::string>()));
      }
      entity = world.instantiate(prefab, name, {}, *uuid);
    } else {
      entity = world.createEntity(name, {}, *uuid);
    }
    loaded.push_back(entity);
    setComponents(world, entity, entry, *uuid, version);
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
  file << sceneText(document);
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
  return loadEntities(world, scene, "scene");
}

namespace {

void appendSceneText(std::string &out, const json &node, std::size_t indent) {
  const bool numbers = node.is_array() && !node.empty() &&
                       std::ranges::all_of(node, [](const json &element) { return element.is_number(); });
  if (numbers) {
    out += '[';
    for (std::size_t i = 0; i < node.size(); ++i) {
      out += i == 0 ? "" : ", ";
      out += node[i].dump();
    }
    out += ']';
  } else if ((node.is_object() || node.is_array()) && !node.empty()) {
    const std::string inner(indent + 2, ' ');
    out += node.is_object() ? "{\n" : "[\n";
    bool first = true;
    for (const auto &[key, value] : node.items()) {
      out += first ? "" : ",\n";
      first = false;
      out += inner;
      if (node.is_object()) {
        out += json(key).dump();
        out += ": ";
      }
      appendSceneText(out, value, indent + 2);
    }
    out += '\n';
    out.append(indent, ' ');
    out += node.is_object() ? '}' : ']';
  } else {
    out += node.dump();
  }
}

} // namespace

std::string sceneText(const json &document) {
  std::string out;
  appendSceneText(out, document, 0);
  out += '\n';
  return out;
}

core::Result<void> saveSceneFile(const World &world, const std::filesystem::path &path) {
  return writeJson(saveScene(world), path);
}

core::Result<std::vector<flecs::entity>> loadSceneFile(World &world, const std::filesystem::path &path) {
  const auto document = readJson(path);
  if (!document) {
    return std::unexpected(document.error());
  }
  auto loaded = loadEntities(world, *document, path.string());
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
  auto loaded = loadEntities(world, prefab, source);
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
  world.makePrefab(root);
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
  root->set<PrefabFile>({path});
  SONNET_LOG_INFO("loaded prefab {} from {}", world.uuidOf(*root).toString(), path.string());
  return root;
}

namespace {

// The identity of a prefab document's root and the prefabs it instantiates, read from the raw
// entries, which every version writes the same way.
struct PrefabLinks {
  std::string root;
  std::vector<std::string> uses;
};

PrefabLinks prefabLinks(const json &document) {
  PrefabLinks links;
  if (!document.is_object() || !document.contains("entities") || !document["entities"].is_array()) {
    return links;
  }
  for (const json &entry : document["entities"]) {
    if (!entry.is_object()) {
      continue;
    }
    if (entry.contains("prefab") && entry["prefab"].is_string()) {
      links.uses.push_back(entry["prefab"].get<std::string>());
    }
    if (links.root.empty() && !entry.contains("parent") && entry.contains("uuid") && entry["uuid"].is_string()) {
      links.root = entry["uuid"].get<std::string>();
    }
  }
  return links;
}

} // namespace

std::vector<core::Error> loadPrefabs(World &world, std::vector<PrefabDocument> documents) {
  std::vector<PrefabLinks> links;
  std::unordered_map<std::string, std::size_t> byRoot;
  for (std::size_t i = 0; i < documents.size(); ++i) {
    links.push_back(prefabLinks(documents[i].document));
    if (!links.back().root.empty()) {
      byRoot.emplace(links.back().root, i);
    }
  }
  // Depth first over the uses, loading a document after the ones it uses. A document met again
  // while its own uses are being loaded closes a cycle: every document on it is refused, with
  // the chain, and the ones using them then fail for want of them.
  enum class State {
    Unvisited,
    Loading,
    Done
  };
  std::vector<State> state(documents.size(), State::Unvisited);
  std::vector<bool> refused(documents.size(), false);
  std::vector<std::size_t> path;
  std::vector<core::Error> errors;
  const auto visit = [&](const auto &self, std::size_t index) -> void {
    if (state[index] == State::Done) {
      return;
    }
    if (state[index] == State::Loading) {
      std::string chain;
      for (auto it = std::ranges::find(path, index); it != path.end(); ++it) {
        chain += std::format("{} -> ", documents[*it].source);
        refused[*it] = true;
      }
      errors.push_back(core::Error{
          std::format("{}: the prefab contains itself: {}{}", documents[index].source, chain, documents[index].source),
          core::ErrorCategory::Io});
      return;
    }
    state[index] = State::Loading;
    path.push_back(index);
    for (const std::string &use : links[index].uses) {
      if (const auto it = byRoot.find(use); it != byRoot.end()) {
        self(self, it->second);
      }
    }
    path.pop_back();
    state[index] = State::Done;
    if (refused[index]) {
      return;
    }
    const auto root = loadPrefabFrom(world, documents[index].document, documents[index].source);
    if (!root) {
      errors.push_back(core::Error{std::format("{}: {}", documents[index].source, root.error().message),
                                   core::ErrorCategory::Io, root.error().location});
    } else if (!documents[index].file.empty()) {
      root->set<PrefabFile>({documents[index].file});
    }
  };
  for (std::size_t i = 0; i < documents.size(); ++i) {
    visit(visit, i);
  }
  return errors;
}

std::vector<core::Error> loadPrefabFiles(World &world, std::span<const std::filesystem::path> paths) {
  std::vector<PrefabDocument> documents;
  std::vector<core::Error> errors;
  for (const std::filesystem::path &path : paths) {
    auto document = readJson(path);
    if (!document) {
      errors.push_back(document.error());
      continue;
    }
    documents.push_back({path.string(), std::move(*document), path});
  }
  std::vector<core::Error> refused = loadPrefabs(world, std::move(documents));
  errors.insert(errors.end(), std::make_move_iterator(refused.begin()), std::make_move_iterator(refused.end()));
  return errors;
}

flecs::entity loadModelPrefab(World &world, const assets::Model &model, const core::Uuid &uuid, std::string_view name) {
  SONNET_ASSERT(!world.find(uuid), "model prefab {} is already loaded", uuid.toString());
  flecs::entity root = world.createEntity(name, {}, uuid);
  std::vector<flecs::entity> entities;
  entities.reserve(model.nodes.size());
  for (std::size_t n = 0; n < model.nodes.size(); ++n) {
    const assets::ModelNode &node = model.nodes[n];
    const flecs::entity parent = node.parent >= 0 ? entities[static_cast<std::size_t>(node.parent)] : root;
    flecs::entity entity = world.createEntity(node.name, parent, core::Uuid::derive(uuid, std::format("node/{}", n)));
    entity.set<Transform>({.position = node.position, .rotation = node.rotation, .scale = node.scale});
    if (!node.mesh.isNil()) {
      entity.set<MeshRenderer>({.mesh = node.mesh, .material = {}, .color = {1.0f, 1.0f, 1.0f, 1.0f}, .visible = true});
    }
    if (!node.skin.isNil()) {
      entity.set<SkinnedMesh>({node.skin});
    }
    if (!node.morphWeights.empty()) {
      entity.set<MorphWeights>({node.morphWeights});
    }
    if (node.camera) {
      entity.set<Camera>({.fovY = node.camera->fovY, .nearPlane = node.camera->nearPlane});
    }
    entities.push_back(entity);
  }
  // A model with clips plays its first on the root, where the clips' paths start.
  if (!model.animations.empty()) {
    root.set<Animator>({.clip = model.animations.front(), .time = 0.0f, .speed = 1.0f, .playing = true, .loop = true});
  }
  world.makePrefab(root);
  return root;
}

} // namespace sonnet::world
