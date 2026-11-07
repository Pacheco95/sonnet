#include "LuaReflection.h"

#include <sonnet/core/Uuid.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <format>
#include <memory>
#include <string_view>
#include <utility>

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
  std::memcpy(static_cast<void *>(&value), data, sizeof(T));
  return value;
}

template <typename T> void write(void *data, T value) {
  std::memcpy(data, &value, sizeof(T));
}

// A conversion error: where in the value it happened, as a path of members and Lua indices
// (`layers[2].weight`), and what was wrong there.
struct Failure {
  std::string path;
  std::string message;
};

// `path` followed by the path below it, with a dot before a member and nothing before an index.
std::string join(std::string path, std::string_view below) {
  if (!below.empty() && !below.starts_with('[')) {
    path += '.';
  }
  path += below;
  return path;
}

std::string describe(const Failure &failure) {
  return failure.path.empty() ? failure.message : std::format("{}: {}", failure.path, failure.message);
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

template <typename Integer> std::optional<Failure> writeInteger(const sol::object &value, void *data) {
  if (value.get_type() != sol::type::number) {
    return Failure{{}, "expected a number"};
  }
  write<Integer>(data, static_cast<Integer>(value.as<lua_Integer>()));
  return std::nullopt;
}

// The collection interface of a type World::registerVector registered: an opaque type that
// reflection shows as a flecs vector. Null for any other type, std::string included.
const EcsOpaque *vectorOf(const flecs::world &world, flecs::entity_t type, flecs::entity_t &element) {
  const auto *opaque = static_cast<const EcsOpaque *>(ecs_get_id(world.c_ptr(), type, ecs_id(EcsOpaque)));
  if (opaque == nullptr || opaque->count == nullptr || opaque->resize == nullptr || opaque->ensure_element == nullptr) {
    return nullptr;
  }
  const auto *vector = static_cast<const EcsVector *>(ecs_get_id(world.c_ptr(), opaque->as_type, ecs_id(EcsVector)));
  if (vector == nullptr) {
    return nullptr;
  }
  element = vector->type;
  return opaque;
}

// A table key as an error message shows it.
std::string describeKey(const sol::object &key) {
  if (key.get_type() == sol::type::string) {
    return std::format("the key \"{}\"", key.as<std::string_view>());
  }
  if (key.get_type() == sol::type::number) {
    return std::format("the key {}", key.as<double>());
  }
  return std::format("a {} key", sol::type_name(key.lua_state(), key.get_type()));
}

// The length of `table` when it is a sequence, keys 1 to n without gaps, or what breaks it.
std::expected<std::size_t, Failure> sequenceLength(const sol::table &table) {
  std::size_t count = 0;
  std::optional<Failure> failure;
  table.for_each([&](const sol::object &key, const sol::object &) {
    ++count;
    if (failure) {
      return;
    }
    // Lua stores a float key with an integral value as an integer, so a number key that is not
    // a whole number from 1 up is not an index.
    const bool index = key.get_type() == sol::type::number && key.as<double>() >= 1.0 &&
                       std::trunc(key.as<double>()) == key.as<double>();
    if (!index) {
      failure = Failure{{}, std::format("expected an array, but the table has {}", describeKey(key))};
    }
  });
  if (failure) {
    return std::unexpected(std::move(*failure));
  }
  // Every key is an index, so with `count` keys any index above `count` leaves one below empty.
  for (std::size_t index = 1; index <= count; ++index) {
    if (table.raw_get<sol::object>(index).get_type() == sol::type::lua_nil) {
      return std::unexpected(Failure{std::format("[{}]", index), "missing, the array has a gap"});
    }
  }
  return count;
}

} // namespace

sol::object toLua(sol::state_view lua, const flecs::world &world, const LuaTypes &types, flecs::entity_t type,
                  const void *data) {
  if (type == types.uuid) {
    const auto uuid = read<core::Uuid>(data);
    return uuid.isNil() ? sol::make_object(lua, sol::lua_nil) : sol::make_object(lua, uuid.toString());
  }
  if (type == types.string) {
    return sol::make_object(lua, *static_cast<const std::string *>(data));
  }
  flecs::entity_t element = 0;
  if (const EcsOpaque *opaque = vectorOf(world, type, element)) {
    const std::size_t count = opaque->count(data);
    sol::table array = lua.create_table(static_cast<int>(count), 0);
    for (std::size_t index = 0; index < count; ++index) {
      // Below the count, ensure_element only returns the element's address.
      array[index + 1] = toLua(lua, world, types, element, opaque->ensure_element(const_cast<void *>(data), index));
    }
    return array;
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

namespace {

std::optional<Failure> assign(const sol::object &value, const flecs::world &world, const LuaTypes &types,
                              flecs::entity_t type, void *data);

// Replaces the collection at `data` with the array `value`: each element starts at the element
// type's default and takes the fields its entry holds. The array's shape is checked before the
// collection changes; an element that fails leaves the ones before it assigned, which
// setFromLua discards with the rest of its copy.
std::optional<Failure> assignVector(const sol::object &value, const flecs::world &world, const LuaTypes &types,
                                    const EcsOpaque &opaque, flecs::entity_t element, void *data) {
  if (value.get_type() != sol::type::table) {
    return Failure{{}, "expected an array"};
  }
  const auto table = value.as<sol::table>();
  const std::expected<std::size_t, Failure> count = sequenceLength(table);
  if (!count) {
    return count.error();
  }
  opaque.resize(data, 0);
  opaque.resize(data, *count);
  for (std::size_t index = 0; index < *count; ++index) {
    if (auto failure =
            assign(table.raw_get<sol::object>(index + 1), world, types, element, opaque.ensure_element(data, index))) {
      return Failure{join(std::format("[{}]", index + 1), failure->path), std::move(failure->message)};
    }
  }
  return std::nullopt;
}

std::optional<Failure> assign(const sol::object &value, const flecs::world &world, const LuaTypes &types,
                              flecs::entity_t type, void *data) {
  if (type == types.uuid) {
    if (value.get_type() == sol::type::lua_nil) {
      write(data, core::Uuid{});
      return std::nullopt;
    }
    if (value.get_type() != sol::type::string) {
      return Failure{{}, "expected an asset identity string or nil"};
    }
    const std::optional<core::Uuid> uuid = core::Uuid::parse(value.as<std::string_view>());
    if (!uuid) {
      return Failure{{}, std::format("\"{}\" is not an asset identity", value.as<std::string_view>())};
    }
    write(data, *uuid);
    return std::nullopt;
  }
  if (type == types.string) {
    if (value.get_type() != sol::type::string) {
      return Failure{{}, "expected a string"};
    }
    *static_cast<std::string *>(data) = value.as<std::string>();
    return std::nullopt;
  }
  flecs::entity_t element = 0;
  if (const EcsOpaque *opaque = vectorOf(world, type, element)) {
    return assignVector(value, world, types, *opaque, element, data);
  }
  const flecs::entity entity{world, type};
  if (const flecs::Primitive *primitive = entity.try_get<flecs::Primitive>()) {
    const flecs::meta::primitive_kind_t kind = primitive->kind;
    if (kind == flecs::meta::Bool) {
      if (value.get_type() != sol::type::boolean) {
        return Failure{{}, "expected a boolean"};
      }
      write(data, value.as<bool>());
      return std::nullopt;
    }
    if (kind == flecs::meta::F32 || kind == flecs::meta::F64) {
      if (value.get_type() != sol::type::number) {
        return Failure{{}, "expected a number"};
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
    return Failure{{}, "this member cannot be written from Lua"};
  }
  if (entity.has<flecs::Enum>()) {
    if (value.get_type() == sol::type::number) {
      write(data, static_cast<std::int32_t>(value.as<lua_Integer>()));
      return std::nullopt;
    }
    if (value.get_type() != sol::type::string) {
      return Failure{{}, "expected a constant name"};
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
      return Failure{{}, std::format("\"{}\" is not one of {}", name, known)};
    }
    write(data, *found);
    return std::nullopt;
  }
  if (const flecs::Struct *layout = entity.try_get<flecs::Struct>()) {
    if (value.get_type() != sol::type::table) {
      return Failure{{}, "expected a table"};
    }
    const auto table = value.as<sol::table>();
    const auto *members = ecs_vec_first_t(&layout->members, ecs_member_t);
    for (std::int32_t i = 0; i < ecs_vec_count(&layout->members); ++i) {
      const ecs_member_t &member = members[i];
      const auto field = table.raw_get<sol::object>(member.name);
      if (member.count > 1 || !field.valid() || field.get_type() == sol::type::lua_nil) {
        continue;
      }
      if (auto failure = assign(field, world, types, member.type, at(data, member.offset))) {
        return Failure{join(member.name, failure->path), std::move(failure->message)};
      }
    }
    return std::nullopt;
  }
  return Failure{{}, "this type cannot be written from Lua"};
}

// Frees a value made by ecs_value_new through the type's hooks.
struct ValueDeleter {
  const flecs::world *world;
  flecs::entity_t type;

  void operator()(void *value) const {
    ecs_value_free(world->c_ptr(), type, value);
  }
};

} // namespace

std::optional<std::string> setFromLua(const sol::object &value, const flecs::world &world, const LuaTypes &types,
                                      flecs::entity target, flecs::entity_t component) {
  // The assignment runs on a copy, so an error part way leaves the entity as it was.
  const std::unique_ptr<void, ValueDeleter> staged{ecs_value_new(world.c_ptr(), component),
                                                   ValueDeleter{&world, component}};
  if (target.has(component)) {
    ecs_value_copy(world.c_ptr(), component, staged.get(), target.get(component));
  }
  if (auto failure = assign(value, world, types, component, staged.get())) {
    return describe(*failure);
  }
  ecs_value_move(world.c_ptr(), component, target.ensure(component), staged.get());
  target.modified(component);
  return std::nullopt;
}

} // namespace sonnet::scripting
