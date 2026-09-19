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
  flecs::entity_t vec3{0};
  flecs::entity_t quat{0};
  sol::table vec3Metatable;
  sol::table quatMetatable;
};

// A reflected value as Lua sees it (docs/scripting.md, "Components"): numbers and booleans as
// themselves, enums as their constant's name, asset identities as their canonical string or nil,
// structs as tables of their members, vec3 and quat with their metatables. Unsupported types
// are nil.
[[nodiscard]] sol::object toLua(sol::state_view lua, const flecs::world &world, const LuaTypes &types,
                                flecs::entity_t type, const void *data);

// Assigns the fields present in `value` to the reflected value at `data`, leaving the others; a
// value of the wrong shape is an error naming the member, and what was assigned before it stays.
[[nodiscard]] std::optional<std::string> fromLua(const sol::object &value, const flecs::world &world,
                                                 const LuaTypes &types, flecs::entity_t type, void *data);

} // namespace sonnet::scripting
