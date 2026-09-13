#include <sonnet/editor/EntityCommands.h>

#include <sonnet/core/Log.h>
#include <sonnet/world/Scene.h>

#include <format>
#include <unordered_map>
#include <utility>

namespace sonnet::editor {

namespace {

using nlohmann::json;

// Every identity in a saved subtree replaced by a fresh one, parents included, so a copy
// coexists with the original. Done once so redo recreates the same copy.
json withFreshIdentities(json document, core::Uuid root) {
  std::unordered_map<std::string, std::string> mapping;
  bool first = true;
  for (json &entry : document["entities"]) {
    const std::string old = entry["uuid"].get<std::string>();
    mapping[old] = (first && !root.isNil() ? root : core::Uuid::generate()).toString();
    entry["uuid"] = mapping[old];
    first = false;
  }
  for (json &entry : document["entities"]) {
    if (entry.contains("parent")) {
      const auto it = mapping.find(entry["parent"].get<std::string>());
      if (it != mapping.end()) {
        entry["parent"] = it->second;
      }
    }
  }
  return document;
}

flecs::entity require(world::World &world, const core::Uuid &uuid, std::string_view what) {
  const flecs::entity entity = world.find(uuid);
  if (!entity) {
    SONNET_LOG_WARN("{}: entity {} no longer exists", what, uuid.toString());
  }
  return entity;
}

// Loads a saved subtree back and places its root under `parent` with the local transform the
// file holds.
flecs::entity restoreSubtree(world::World &world, const json &document, const core::Uuid &parent,
                             std::string_view what) {
  const auto loaded = world::loadScene(world, document);
  if (!loaded) {
    SONNET_LOG_ERROR("{}: {}", what, loaded.error().toString());
    return {};
  }
  flecs::entity root;
  for (const flecs::entity entity : *loaded) {
    if (!entity.parent()) {
      root = entity;
    }
  }
  if (root && !parent.isNil()) {
    if (const flecs::entity parentEntity = world.find(parent)) {
      root.child_of(parentEntity);
    }
  }
  return root;
}

class CreateEntityCommand final : public ICommand {
public:
  CreateEntityCommand(std::string name, core::Uuid parent, json components, core::Uuid uuid)
      : m_name(std::move(name)), m_parent(parent), m_components(std::move(components)),
        m_uuid(uuid.isNil() ? core::Uuid::generate() : uuid), m_description(std::format("create {}", m_name)) {
  }

  void apply(world::World &world) override {
    const flecs::entity entity = world.createEntity(m_name, world.find(m_parent), m_uuid);
    for (const auto &[name, value] : m_components.items()) {
      if (const world::ComponentInfo *info = world.findComponent(name)) {
        world.componentFromJson(entity, info->id, value);
      }
    }
  }
  void revert(world::World &world) override {
    world.destroyEntity(require(world, m_uuid, m_description));
  }
  std::string_view description() const override {
    return m_description;
  }

private:
  std::string m_name;
  core::Uuid m_parent;
  json m_components;
  core::Uuid m_uuid;
  std::string m_description;
};

class DeleteEntityCommand final : public ICommand {
public:
  explicit DeleteEntityCommand(core::Uuid entity) : m_entity(entity) {
  }

  void apply(world::World &world) override {
    const flecs::entity entity = require(world, m_entity, "delete");
    if (!entity) {
      return;
    }
    if (m_description.empty()) {
      const world::Name *name = entity.try_get<world::Name>();
      m_description = std::format("delete {}", name != nullptr ? name->value : m_entity.toString());
    }
    m_snapshot = world::saveSubtree(world, entity);
    m_parent = world.uuidOf(world.parentOf(entity));
    world.destroyEntity(entity);
  }
  void revert(world::World &world) override {
    restoreSubtree(world, m_snapshot, m_parent, m_description);
  }
  std::string_view description() const override {
    return m_description.empty() ? std::string_view{"delete"} : std::string_view{m_description};
  }

private:
  core::Uuid m_entity;
  core::Uuid m_parent;
  json m_snapshot;
  std::string m_description;
};

class DuplicateEntityCommand final : public ICommand {
public:
  DuplicateEntityCommand(core::Uuid source, core::Uuid copy) : m_source(source), m_root(copy) {
  }

  void apply(world::World &world) override {
    if (m_copy.is_null()) {
      const flecs::entity source = require(world, m_source, "duplicate");
      if (!source) {
        return;
      }
      m_copy = withFreshIdentities(world::saveSubtree(world, source), m_root);
      json &root = m_copy["entities"][0];
      root["name"] = root["name"].get<std::string>() + " copy";
      m_root = *core::Uuid::parse(root["uuid"].get<std::string>());
      m_parent = world.uuidOf(world.parentOf(source));
      m_description = std::format("duplicate {}", source.try_get<world::Name>()->value);
    }
    restoreSubtree(world, m_copy, m_parent, m_description);
  }
  void revert(world::World &world) override {
    world.destroyEntity(require(world, m_root, m_description));
  }
  std::string_view description() const override {
    return m_description.empty() ? std::string_view{"duplicate"} : std::string_view{m_description};
  }

private:
  core::Uuid m_source;
  core::Uuid m_root;
  core::Uuid m_parent;
  json m_copy;
  std::string m_description;
};

class ReparentCommand final : public ICommand {
public:
  ReparentCommand(core::Uuid entity, core::Uuid parent) : m_entity(entity), m_parent(parent) {
  }

  void apply(world::World &world) override {
    const flecs::entity entity = require(world, m_entity, "reparent");
    if (!entity) {
      return;
    }
    if (m_before.is_null()) {
      m_previousParent = world.uuidOf(world.parentOf(entity));
      m_before = world.componentToJson(entity, world.findComponent("Transform")->id);
      m_description = std::format("reparent {}", entity.try_get<world::Name>()->value);
    }
    world.setParent(entity, world.find(m_parent));
  }
  void revert(world::World &world) override {
    const flecs::entity entity = require(world, m_entity, m_description);
    if (!entity) {
      return;
    }
    world.setParent(entity, world.find(m_previousParent));
    world.componentFromJson(entity, world.findComponent("Transform")->id, m_before);
  }
  std::string_view description() const override {
    return m_description.empty() ? std::string_view{"reparent"} : std::string_view{m_description};
  }

private:
  core::Uuid m_entity;
  core::Uuid m_parent;
  core::Uuid m_previousParent;
  json m_before;
  std::string m_description;
};

class RenameCommand final : public ICommand {
public:
  RenameCommand(core::Uuid entity, std::string before, std::string after)
      : m_entity(entity), m_before(std::move(before)), m_after(std::move(after)),
        m_description(std::format("rename {} to {}", m_before, m_after)) {
  }

  void apply(world::World &world) override {
    if (const flecs::entity entity = require(world, m_entity, m_description)) {
      entity.set<world::Name>({m_after});
    }
  }
  void revert(world::World &world) override {
    if (const flecs::entity entity = require(world, m_entity, m_description)) {
      entity.set<world::Name>({m_before});
    }
  }
  std::string_view description() const override {
    return m_description;
  }

private:
  core::Uuid m_entity;
  std::string m_before;
  std::string m_after;
  std::string m_description;
};

class ComponentCommand final : public ICommand {
public:
  ComponentCommand(core::Uuid entity, std::string component, std::optional<json> before, std::optional<json> after,
                   std::string description)
      : m_entity(entity), m_component(std::move(component)), m_before(std::move(before)), m_after(std::move(after)),
        m_description(std::move(description)) {
  }

  void apply(world::World &world) override {
    set(world, m_after);
  }
  void revert(world::World &world) override {
    set(world, m_before);
  }
  std::string_view description() const override {
    return m_description;
  }

private:
  void set(world::World &world, const std::optional<json> &value) {
    const flecs::entity entity = require(world, m_entity, m_description);
    const world::ComponentInfo *info = world.findComponent(m_component);
    if (!entity || info == nullptr) {
      return;
    }
    if (!value) {
      entity.remove(info->id);
    } else {
      world.componentFromJson(entity, info->id, *value);
    }
  }

  core::Uuid m_entity;
  std::string m_component;
  std::optional<json> m_before;
  std::optional<json> m_after;
  std::string m_description;
};

class InstantiatePrefabCommand final : public ICommand {
public:
  InstantiatePrefabCommand(core::Uuid prefab, std::string name, core::Uuid parent, core::Uuid uuid)
      : m_prefab(prefab), m_name(std::move(name)), m_parent(parent),
        m_uuid(uuid.isNil() ? core::Uuid::generate() : uuid), m_description(std::format("instantiate {}", m_name)) {
  }

  void apply(world::World &world) override {
    const flecs::entity prefab = require(world, m_prefab, m_description);
    if (!prefab || !prefab.has(flecs::Prefab)) {
      return;
    }
    const flecs::entity entity = world.instantiate(prefab, m_name, world.find(m_parent));
    entity.set<world::Identity>({m_uuid});
  }
  void revert(world::World &world) override {
    world.destroyEntity(require(world, m_uuid, m_description));
  }
  std::string_view description() const override {
    return m_description;
  }

private:
  core::Uuid m_prefab;
  std::string m_name;
  core::Uuid m_parent;
  core::Uuid m_uuid;
  std::string m_description;
};

class CompositeCommand final : public ICommand {
public:
  CompositeCommand(std::string description, std::vector<std::unique_ptr<ICommand>> commands)
      : m_description(std::move(description)), m_commands(std::move(commands)) {
  }

  void apply(world::World &world) override {
    for (auto &command : m_commands) {
      command->apply(world);
    }
  }
  void revert(world::World &world) override {
    for (auto it = m_commands.rbegin(); it != m_commands.rend(); ++it) {
      (*it)->revert(world);
    }
  }
  std::string_view description() const override {
    return m_description;
  }

private:
  std::string m_description;
  std::vector<std::unique_ptr<ICommand>> m_commands;
};

} // namespace

std::unique_ptr<ICommand> createEntityCommand(std::string name, core::Uuid parent, json components, core::Uuid uuid) {
  return std::make_unique<CreateEntityCommand>(std::move(name), parent, std::move(components), uuid);
}

std::unique_ptr<ICommand> deleteEntityCommand(core::Uuid entity) {
  return std::make_unique<DeleteEntityCommand>(entity);
}

std::unique_ptr<ICommand> duplicateEntityCommand(core::Uuid entity, core::Uuid copy) {
  return std::make_unique<DuplicateEntityCommand>(entity, copy);
}

std::unique_ptr<ICommand> reparentCommand(core::Uuid entity, core::Uuid parent) {
  return std::make_unique<ReparentCommand>(entity, parent);
}

std::unique_ptr<ICommand> renameCommand(core::Uuid entity, std::string before, std::string after) {
  return std::make_unique<RenameCommand>(entity, std::move(before), std::move(after));
}

std::unique_ptr<ICommand> componentCommand(core::Uuid entity, std::string component, std::optional<json> before,
                                           std::optional<json> after, std::string description) {
  return std::make_unique<ComponentCommand>(entity, std::move(component), std::move(before), std::move(after),
                                            std::move(description));
}

std::unique_ptr<ICommand> instantiatePrefabCommand(core::Uuid prefab, std::string name, core::Uuid parent,
                                                   core::Uuid uuid) {
  return std::make_unique<InstantiatePrefabCommand>(prefab, std::move(name), parent, uuid);
}

std::unique_ptr<ICommand> compositeCommand(std::string description, std::vector<std::unique_ptr<ICommand>> commands) {
  return std::make_unique<CompositeCommand>(std::move(description), std::move(commands));
}

} // namespace sonnet::editor
