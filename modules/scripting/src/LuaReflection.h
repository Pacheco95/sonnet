#pragma once

#include <flecs.h>
#include <sol/sol.hpp>

#include <optional>
#include <string>

namespace sonnet::scripting {

// What the conversions need to know beyond flecs' reflection: the types that are not plain
// structs to Lua, and the metatables vec3 and quat values carry so their operators work.
struct LuaTypes {
  flecs::entity_t uuid{0};
  flecs::entity_t string{0}; // std::string, which flecs reflects as an opaque type
  flecs::entity_t vec3{0};
  flecs::entity_t quat{0};
  sol::table vec3Metatable;
  sol::table quatMetatable;
};

// A reflected value as Lua sees it (docs/scripting.md, "Components"): numbers and booleans as
// themselves, strings as strings, enums as their constant's name, asset identities as their canonical string or nil,
// structs as tables of their members, vec3 and quat with their metatables, collections from
// World::registerVector as arrays of their elements. Unsupported types are nil.
[[nodiscard]] sol::object toLua(sol::state_view lua, const flecs::world &world, const LuaTypes &types,
                                flecs::entity_t type, const void *data);

// Assigns the fields present in `value` to `target`'s `component`, leaving the others and adding
// the component when absent; an array replaces a collection, its elements starting from their
// defaults. A value of the wrong shape is an error naming its path (`layers[2].weight`), and
// leaves the component as it was: the fields are assigned to a copy that replaces the component
// only when every one converted.
[[nodiscard]] std::optional<std::string> setFromLua(const sol::object &value, const flecs::world &world,
                                                    const LuaTypes &types, flecs::entity target,
                                                    flecs::entity_t component);

} // namespace sonnet::scripting
