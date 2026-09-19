#include "LuaReflection.h"

#include <sonnet/core/Uuid.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <string_view>

namespace sonnet::scripting {

namespace {

std::byte *at(void *data, std::int32_t offset) {
  return static_cast<std::byte *>(data) + offset;
}

const std::byte *at(const void *data, std::int32_t offset) {
  return static_cast<const std::byte *>(data) + offset;
}

template <typename T> T read(const void *data) {
  T value;
  std::memcpy(&value, data, sizeof(T));
  return value;
}

template <typename T> void write(void *data, T value) {
  std::memcpy(data, &value, sizeof(T));
}

// The enum's constants as (name, value) through a callback; flecs keeps them as child entities
// with a (Constant, i32) pair.
template <typename Visit> void forEachConstant(const flecs::entity &type, Visit &&visit) {
  type.children([&](flecs::entity constant) {
    if (const std::int32_t *value = constant.try_get_second<std::int32_t>(flecs::Constant)) {
      visit(std::string_view{constant.name().c_str()}, *value);
    }
  });
}

template <typename Integer> std::optional<std::string> writeInteger(const sol::object &value, void *data) {
  if (value.get_type() != sol::type::number) {
    return "expected a number";
  }
  write<Integer>(data, static_cast<Integer>(value.as<lua_Integer>()));
  return std::nullopt;
}

} // namespace

sol::object toLua(sol::state_view lua, const flecs::world &world, const LuaTypes &types, flecs::entity_t type,
                  const void *data) {
  if (type == types.uuid) {
    const auto uuid = read<core::Uuid>(data);
    return uuid.isNil() ? sol::make_object(lua, sol::lua_nil) : sol::make_object(lua, uuid.toString());
  }
  const flecs::entity entity{world, type};
  if (const flecs::Primitive *primitive = entity.try_get<flecs::Primitive>()) {
    // The kinds are not constant expressions, hence no switch.
    const flecs::meta::primitive_kind_t kind = primitive->kind;
    if (kind == flecs::meta::Bool) {
      return sol::make_object(lua, read<bool>(data));
    }
    if (kind == flecs::meta::F32) {
      return sol::make_object(lua, static_cast<double>(read<float>(data)));
    }
    if (kind == flecs::meta::F64) {
      return sol::make_object(lua, read<double>(data));
    }
    if (kind == flecs::meta::I8) {
      return sol::make_object(lua, static_cast<lua_Integer>(read<std::int8_t>(data)));
    }
    if (kind == flecs::meta::I16) {
      return sol::make_object(lua, static_cast<lua_Integer>(read<std::int16_t>(data)));
    }
    if (kind == flecs::meta::I32) {
      return sol::make_object(lua, static_cast<lua_Integer>(read<std::int32_t>(data)));
    }
    if (kind == flecs::meta::I64) {
      return sol::make_object(lua, static_cast<lua_Integer>(read<std::int64_t>(data)));
    }
    if (kind == flecs::meta::U8 || kind == flecs::meta::Byte || kind == flecs::meta::Char) {
      return sol::make_object(lua, static_cast<lua_Integer>(read<std::uint8_t>(data)));
    }
    if (kind == flecs::meta::U16) {
      return sol::make_object(lua, static_cast<lua_Integer>(read<std::uint16_t>(data)));
    }
    if (kind == flecs::meta::U32) {
      return sol::make_object(lua, static_cast<lua_Integer>(read<std::uint32_t>(data)));
    }
    if (kind == flecs::meta::U64) {
      return sol::make_object(lua, static_cast<lua_Integer>(read<std::uint64_t>(data)));
    }
    if (kind == flecs::meta::String) {
      const char *text = read<const char *>(data);
      return text != nullptr ? sol::make_object(lua, text) : sol::make_object(lua, sol::lua_nil);
    }
    return sol::make_object(lua, sol::lua_nil);
  }
  if (entity.has<flecs::Enum>()) {
    const auto value = read<std::int32_t>(data);
    std::string name;
    forEachConstant(entity, [&](std::string_view constant, std::int32_t constantValue) {
      if (constantValue == value) {
        name = constant;
      }
    });
    return name.empty() ? sol::make_object(lua, static_cast<lua_Integer>(value)) : sol::make_object(lua, name);
  }
  if (const flecs::Struct *layout = entity.try_get<flecs::Struct>()) {
    sol::table table = lua.create_table();
    const auto *members = ecs_vec_first_t(&layout->members, ecs_member_t);
    for (std::int32_t i = 0; i < ecs_vec_count(&layout->members); ++i) {
      const ecs_member_t &member = members[i];
      if (member.count <= 1) {
        table[member.name] = toLua(lua, world, types, member.type, at(data, member.offset));
      }
    }
    if (type == types.vec3) {
      table[sol::metatable_key] = types.vec3Metatable;
    } else if (type == types.quat) {
      table[sol::metatable_key] = types.quatMetatable;
    }
    return table;
  }
  return sol::make_object(lua, sol::lua_nil);
}

std::optional<std::string> fromLua(const sol::object &value, const flecs::world &world, const LuaTypes &types,
                                   flecs::entity_t type, void *data) {
  if (type == types.uuid) {
    if (value.get_type() == sol::type::lua_nil) {
      write(data, core::Uuid{});
      return std::nullopt;
    }
    if (value.get_type() != sol::type::string) {
      return "expected an asset identity string or nil";
    }
    const std::optional<core::Uuid> uuid = core::Uuid::parse(value.as<std::string_view>());
    if (!uuid) {
      return std::format("\"{}\" is not an asset identity", value.as<std::string_view>());
    }
    write(data, *uuid);
    return std::nullopt;
  }
  const flecs::entity entity{world, type};
  if (const flecs::Primitive *primitive = entity.try_get<flecs::Primitive>()) {
    const flecs::meta::primitive_kind_t kind = primitive->kind;
    if (kind == flecs::meta::Bool) {
      if (value.get_type() != sol::type::boolean) {
        return "expected a boolean";
      }
      write(data, value.as<bool>());
      return std::nullopt;
    }
    if (kind == flecs::meta::F32 || kind == flecs::meta::F64) {
      if (value.get_type() != sol::type::number) {
        return "expected a number";
      }
      if (kind == flecs::meta::F32) {
        write(data, static_cast<float>(value.as<double>()));
      } else {
        write(data, value.as<double>());
      }
      return std::nullopt;
    }
    if (kind == flecs::meta::I8) {
      return writeInteger<std::int8_t>(value, data);
    }
    if (kind == flecs::meta::I16) {
      return writeInteger<std::int16_t>(value, data);
    }
    if (kind == flecs::meta::I32) {
      return writeInteger<std::int32_t>(value, data);
    }
    if (kind == flecs::meta::I64) {
      return writeInteger<std::int64_t>(value, data);
    }
    if (kind == flecs::meta::U8 || kind == flecs::meta::Byte || kind == flecs::meta::Char) {
      return writeInteger<std::uint8_t>(value, data);
    }
    if (kind == flecs::meta::U16) {
      return writeInteger<std::uint16_t>(value, data);
    }
    if (kind == flecs::meta::U32) {
      return writeInteger<std::uint32_t>(value, data);
    }
    if (kind == flecs::meta::U64) {
      return writeInteger<std::uint64_t>(value, data);
    }
    return "this member cannot be written from Lua";
  }
  if (entity.has<flecs::Enum>()) {
    if (value.get_type() == sol::type::number) {
      write(data, static_cast<std::int32_t>(value.as<lua_Integer>()));
      return std::nullopt;
    }
    if (value.get_type() != sol::type::string) {
      return "expected a constant name";
    }
    const auto name = value.as<std::string_view>();
    std::optional<std::int32_t> found;
    std::string known;
    forEachConstant(entity, [&](std::string_view constant, std::int32_t constantValue) {
      if (constant == name) {
        found = constantValue;
      }
      known += known.empty() ? std::string{constant} : std::format(", {}", constant);
    });
    if (!found) {
      return std::format("\"{}\" is not one of {}", name, known);
    }
    write(data, *found);
    return std::nullopt;
  }
  if (const flecs::Struct *layout = entity.try_get<flecs::Struct>()) {
    if (value.get_type() != sol::type::table) {
      return "expected a table";
    }
    const sol::table table = value.as<sol::table>();
    const auto *members = ecs_vec_first_t(&layout->members, ecs_member_t);
    for (std::int32_t i = 0; i < ecs_vec_count(&layout->members); ++i) {
      const ecs_member_t &member = members[i];
      const sol::object field = table.raw_get<sol::object>(member.name);
      if (member.count > 1 || !field.valid() || field.get_type() == sol::type::lua_nil) {
        continue;
      }
      if (auto error = fromLua(field, world, types, member.type, at(data, member.offset))) {
        return std::format("{}: {}", member.name, *error);
      }
    }
    return std::nullopt;
  }
  return "this type cannot be written from Lua";
}

} // namespace sonnet::scripting
