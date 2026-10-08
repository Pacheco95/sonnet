#include <sonnet/editor/ScriptSlots.h>

#include <sonnet/editor/EntityCommands.h>

#include <algorithm>
#include <format>

namespace sonnet::editor {

namespace {

// A slot's properties as an object; anything else, including the empty text, is an empty one.
nlohmann::json parse(const std::string &text) {
  if (text.empty()) {
    return nlohmann::json::object();
  }
  nlohmann::json json = nlohmann::json::parse(text, nullptr, false);
  return json.is_object() ? json : nlohmann::json::object();
}

// Compact, with sorted keys, and nothing at all for no changes.
std::string serialize(const nlohmann::json &object) {
  return object.empty() ? std::string{} : object.dump();
}

} // namespace

scripting::Scripts appendSlot(scripting::Scripts scripts, const core::Uuid &script) {
  scripts.slots.push_back({.script = script});
  return scripts;
}

scripting::Scripts removeSlot(scripting::Scripts scripts, std::size_t index) {
  if (index < scripts.slots.size()) {
    scripts.slots.erase(scripts.slots.begin() + static_cast<std::ptrdiff_t>(index));
  }
  return scripts;
}

scripting::Scripts moveSlot(scripting::Scripts scripts, std::size_t from, std::size_t to) {
  if (from >= scripts.slots.size() || to >= scripts.slots.size() || from == to) {
    return scripts;
  }
  const auto first = scripts.slots.begin();
  if (from < to) {
    std::rotate(first + static_cast<std::ptrdiff_t>(from), first + static_cast<std::ptrdiff_t>(from) + 1,
                first + static_cast<std::ptrdiff_t>(to) + 1);
  } else {
    std::rotate(first + static_cast<std::ptrdiff_t>(to), first + static_cast<std::ptrdiff_t>(from),
                first + static_cast<std::ptrdiff_t>(from) + 1);
  }
  return scripts;
}

nlohmann::json propertyValue(const scripting::ScriptSlot &slot, const scripting::PropertyDecl &property) {
  const nlohmann::json values = parse(slot.properties);
  const auto found = values.find(property.name);
  return found != values.end() ? *found : property.defaultValue;
}

bool propertyChanged(const scripting::ScriptSlot &slot, const scripting::PropertyDecl &property) {
  return parse(slot.properties).contains(property.name);
}

scripting::ScriptSlot setProperty(scripting::ScriptSlot slot, const scripting::PropertyDecl &property,
                                  const nlohmann::json &value) {
  nlohmann::json values = parse(slot.properties);
  if (value == property.defaultValue) {
    values.erase(property.name);
  } else {
    values[property.name] = value;
  }
  slot.properties = serialize(values);
  return slot;
}

scripting::ScriptSlot revertProperty(scripting::ScriptSlot slot, const scripting::PropertyDecl &property) {
  nlohmann::json values = parse(slot.properties);
  values.erase(property.name);
  slot.properties = serialize(values);
  return slot;
}

std::vector<std::string> undeclaredProperties(const scripting::ScriptSlot &slot,
                                              const std::vector<scripting::PropertyDecl> &properties) {
  std::vector<std::string> names;
  const nlohmann::json values = parse(slot.properties);
  for (const auto &[name, value] : values.items()) {
    if (std::ranges::none_of(properties, [&](const scripting::PropertyDecl &p) { return p.name == name; })) {
      names.push_back(name);
    }
  }
  return names;
}

std::unique_ptr<ICommand> appendScriptCommand(const world::World &world, const core::Uuid &entity,
                                              const core::Uuid &script) {
  const flecs::entity target = world.find(entity);
  const world::ComponentInfo *info = world.findComponent("Scripts");
  if (!target || info == nullptr) {
    return nullptr;
  }
  const scripting::Scripts *current = target.try_get<scripting::Scripts>();
  const scripting::Scripts after = appendSlot(current != nullptr ? *current : scripting::Scripts{}, script);
  std::optional<nlohmann::json> before;
  if (current != nullptr) {
    before.emplace(world.componentToJson(target, info->id));
  }
  return componentCommand(entity, "Scripts", std::move(before), std::make_optional(world.valueToJson(info->id, &after)),
                          current != nullptr ? "add a script slot" : "add a script");
}

} // namespace sonnet::editor
