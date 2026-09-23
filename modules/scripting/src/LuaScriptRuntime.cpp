#include <sonnet/scripting/ScriptRuntime.h>

#include "LuaReflection.h"

#include <sonnet/assets/AssetDatabase.h>
#include <sonnet/core/Log.h>
#include <sonnet/core/Profile.h>
#include <sonnet/physics/PhysicsWorld.h>
#include <sonnet/platform/InputState.h>
#include <sonnet/world/Components.h>
#include <sonnet/world/World.h>

#include <sol/sol.hpp>

#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace sonnet::scripting {

namespace {

// vec3 and quat for scripts: plain tables with metatables, so values read from components and
// values made in Lua are the same kind of thing (docs/scripting.md, "Maths"). Returns the two
// metatables, which reflection gives to vec3 and quat members.
constexpr std::string_view Prelude = R"lua(
local Vec3 = {}
Vec3.__index = Vec3
local function newVec3(x, y, z)
  return setmetatable({ x = x or 0, y = y or 0, z = z or 0 }, Vec3)
end
Vec3.__add = function(a, b) return newVec3(a.x + b.x, a.y + b.y, a.z + b.z) end
Vec3.__sub = function(a, b) return newVec3(a.x - b.x, a.y - b.y, a.z - b.z) end
Vec3.__unm = function(a) return newVec3(-a.x, -a.y, -a.z) end
Vec3.__mul = function(a, b)
  if type(a) == "number" then return newVec3(a * b.x, a * b.y, a * b.z) end
  if type(b) == "number" then return newVec3(a.x * b, a.y * b, a.z * b) end
  return newVec3(a.x * b.x, a.y * b.y, a.z * b.z)
end
Vec3.__div = function(a, b)
  if type(b) == "number" then return newVec3(a.x / b, a.y / b, a.z / b) end
  return newVec3(a.x / b.x, a.y / b.y, a.z / b.z)
end
Vec3.__eq = function(a, b) return a.x == b.x and a.y == b.y and a.z == b.z end
Vec3.__tostring = function(a) return string.format("vec3(%g, %g, %g)", a.x, a.y, a.z) end
function Vec3.dot(a, b) return a.x * b.x + a.y * b.y + a.z * b.z end
function Vec3.cross(a, b)
  return newVec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x)
end
function Vec3.length(a) return math.sqrt(a.x * a.x + a.y * a.y + a.z * a.z) end
function Vec3.normalized(a)
  local length = Vec3.length(a)
  if length > 0 then return newVec3(a.x / length, a.y / length, a.z / length) end
  return newVec3(0, 0, 0)
end
function Vec3.lerp(a, b, t)
  return newVec3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t)
end

local Quat = {}
Quat.__index = Quat
local function newQuat(x, y, z, w)
  return setmetatable({ x = x or 0, y = y or 0, z = z or 0, w = w or 1 }, Quat)
end
function Quat.identity() return newQuat(0, 0, 0, 1) end
function Quat.axisAngle(axis, angle)
  local n = Vec3.normalized(axis)
  local s = math.sin(angle * 0.5)
  return newQuat(n.x * s, n.y * s, n.z * s, math.cos(angle * 0.5))
end
-- Yaw about Y, then pitch about X, then roll about Z, in radians.
function Quat.euler(pitch, yaw, roll)
  return Quat.axisAngle(newVec3(0, 1, 0), yaw) * Quat.axisAngle(newVec3(1, 0, 0), pitch)
      * Quat.axisAngle(newVec3(0, 0, 1), roll)
end
Quat.__mul = function(a, b)
  if b.w ~= nil then
    return newQuat(a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                   a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                   a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
                   a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z)
  end
  -- A vector is rotated: v + 2w (u x v) + 2 u x (u x v).
  local u = newVec3(a.x, a.y, a.z)
  local v = newVec3(b.x, b.y, b.z)
  local t = Vec3.cross(u, v) * 2
  return v + t * a.w + Vec3.cross(u, t)
end
Quat.__eq = function(a, b) return a.x == b.x and a.y == b.y and a.z == b.z and a.w == b.w end
Quat.__tostring = function(q) return string.format("quat(%g, %g, %g, %g)", q.x, q.y, q.z, q.w) end
function Quat.inverse(q)
  local n = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w
  return newQuat(-q.x / n, -q.y / n, -q.z / n, q.w / n)
end
function Quat.normalized(q)
  local n = math.sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w)
  if n > 0 then return newQuat(q.x / n, q.y / n, q.z / n, q.w / n) end
  return Quat.identity()
end

vec3 = setmetatable({}, { __index = Vec3, __call = function(_, x, y, z) return newVec3(x, y, z) end })
quat = setmetatable({}, { __index = Quat, __call = function(_, x, y, z, w) return newQuat(x, y, z, w) end })
return Vec3, Quat
)lua";

// An entity as scripts hold it: the flecs id with its generation, so a deleted entity is
// reported as gone rather than aliasing whatever reuses its slot.
struct LuaEntity {
  flecs::entity_t id{0};
};

// Raises a Lua error; with Lua compiled as C++ this throws Lua's own exception, which unwinds to
// the protected call that started the script.
[[noreturn]] void raise(lua_State *state, const std::string &message) {
  luaL_error(state, "%s", message.c_str());
  std::abort(); // luaL_error does not return
}

// "line: message" split into the line and the message.
std::optional<std::pair<int, std::string_view>> splitLine(std::string_view rest) {
  int line = 0;
  const auto [end, error] = std::from_chars(rest.data(), rest.data() + rest.size(), line);
  if (error != std::errc{} || end == rest.data() || end == rest.data() + rest.size() || *end != ':') {
    return std::nullopt;
  }
  std::string_view text = rest.substr(static_cast<std::size_t>(end - rest.data()) + 1);
  if (text.starts_with(' ')) {
    text.remove_prefix(1);
  }
  return std::pair{line, text};
}

// Lua's "path:line: message" split into the line and the message, when the error is in the file
// at `path`. Lua shortens a path longer than LUA_IDSIZE to "..." and its tail, which a project
// under a long temporary directory reaches.
std::optional<std::pair<int, std::string_view>> locate(std::string_view message, std::string_view path) {
  if (path.empty()) {
    return std::nullopt;
  }
  if (message.starts_with(path) && message.size() > path.size() && message[path.size()] == ':') {
    return splitLine(message.substr(path.size() + 1));
  }
  static constexpr std::string_view Ellipsis = "...";
  if (!message.starts_with(Ellipsis)) {
    return std::nullopt;
  }
  for (std::size_t colon = message.find(':', Ellipsis.size()); colon != std::string_view::npos;
       colon = message.find(':', colon + 1)) {
    const std::string_view tail = message.substr(Ellipsis.size(), colon - Ellipsis.size());
    if (!tail.empty() && path.ends_with(tail)) {
      if (const auto located = splitLine(message.substr(colon + 1))) {
        return located;
      }
    }
  }
  return std::nullopt;
}

class LuaScriptRuntime final : public IScriptRuntime {
public:
  explicit LuaScriptRuntime(const ScriptDesc &desc)
      : m_world(*desc.world), m_assets(*desc.assets), m_physics(desc.physics), m_input(desc.input) {
    registerComponents(m_world);
    // No io, os, package or debug: scripts reach the engine through its tables only, the same on
    // every platform the player runs on (ADR-0009).
    m_lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table, sol::lib::utf8,
                         sol::lib::coroutine);
    m_lua["dofile"] = sol::lua_nil;
    m_lua["loadfile"] = sol::lua_nil;

    sol::protected_function_result prelude = m_lua.safe_script(Prelude, sol::script_pass_on_error, "=prelude");
    if (!prelude.valid()) {
      const sol::error error = prelude.get<sol::error>();
      throw core::Exception{std::format("the scripting prelude failed to load: {}", error.what()),
                            core::ErrorCategory::Script};
    }
    flecs::world &ecs = m_world.ecs();
    m_types.uuid = ecs.id<core::Uuid>();
    m_types.vec3 = ecs.id<glm::vec3>();
    m_types.quat = ecs.id<glm::quat>();
    m_types.vec3Metatable = prelude.get<sol::table>(0);
    m_types.quatMetatable = prelude.get<sol::table>(1);
    bindLog();
    bindEntity();
    bindWorld();
    if (m_input != nullptr) {
      bindInput();
    }
    if (m_physics != nullptr) {
      bindPhysics();
    }

    m_scripts = ecs.query_builder<const Script>("Scripts").without<world::Disabled>().build();
    // Immediate, with deferring suspended: a script sees its own changes on its next line, such
    // as a component it just added or the children of a prefab it just instantiated. flecs runs
    // even immediate systems deferred; these iterate no query, so suspending is safe.
    m_fixedSystem = ecs.system("ScriptFixedUpdate")
                        .kind(m_world.phase(world::Phase::FixedUpdate))
                        .immediate()
                        .run([this](flecs::iter &it) { frame("fixedUpdate", it.delta_time()); });
    m_world.addToSimulation(m_fixedSystem);
    m_updateSystem =
        ecs.system("ScriptUpdate").kind(m_world.phase(world::Phase::Update)).immediate().run([this](flecs::iter &it) {
          frame("update", it.delta_time());
        });
    m_world.addToSimulation(m_updateSystem);
    SONNET_LOG_DEBUG("scripting ready, {}", LUA_RELEASE);
  }

  ~LuaScriptRuntime() override {
    m_fixedSystem.destruct();
    m_updateSystem.destruct();
    m_scripts.destruct();
  }

  LuaScriptRuntime(const LuaScriptRuntime &) = delete;
  LuaScriptRuntime &operator=(const LuaScriptRuntime &) = delete;

  void reset() override {
    m_instances.clear();
    m_classes.clear();
    m_lua.collect_garbage();
  }

  void seedRandom(std::uint64_t seed) override {
    // As a script would: math.randomseed with one integer.
    const auto randomseed = m_lua["math"]["randomseed"].get<sol::protected_function>();
    static_cast<void>(randomseed(static_cast<lua_Integer>(seed)));
  }

  core::Result<void> run(std::string_view code, std::string_view chunkName) override {
    sol::load_result chunk = m_lua.load(code, std::format("={}", chunkName), sol::load_mode::text);
    if (!chunk.valid()) {
      const sol::error error = chunk.get<sol::error>();
      return std::unexpected(core::Error{error.what(), core::ErrorCategory::Script});
    }
    sol::protected_function function = chunk.get<sol::protected_function>();
    const sol::environment environment{m_lua, sol::create, m_lua.globals()};
    sol::set_environment(environment, function);
    const sol::protected_function_result result = function();
    if (!result.valid()) {
      const sol::error error = result.get<sol::error>();
      return std::unexpected(core::Error{error.what(), core::ErrorCategory::Script});
    }
    return {};
  }

  std::uint32_t instanceCount() const override {
    return static_cast<std::uint32_t>(m_instances.size());
  }

private:
  struct ScriptClass {
    std::uint64_t revision{0}; // of the source last tried, loaded or not
    std::string path;
    sol::environment environment;
    sol::table table; // invalid until a revision loads
    bool missing{false};
  };
  struct Instance {
    core::Uuid script;
    sol::table self;
    sol::table metatable; // its __index is the class, swapped on reload
    bool started{false};
    bool failed{false};
  };

  // ---- Frames ----

  void frame(const char *hook, float dt) {
    SONNET_ZONE();
    flecs::world &ecs = m_world.ecs();
    ecs.defer_suspend();
    sync();
    for (auto &[id, instance] : m_instances) {
      // A script earlier in this frame may have destroyed the entity.
      if (instance.started && ecs.is_alive(id)) {
        call(id, instance, hook, dt);
      }
    }
    ecs.defer_resume();
  }

  // Instances for the enabled entities with a Script, loaded or reloaded classes, and start for
  // the new instances.
  void sync() {
    flecs::world &ecs = m_world.ecs();
    std::vector<std::pair<flecs::entity_t, core::Uuid>> wanted;
    m_scripts.each(
        [&](flecs::entity entity, const Script &script) { wanted.emplace_back(entity.id(), script.script); });
    for (auto it = m_instances.begin(); it != m_instances.end();) {
      const Script *script = ecs.is_alive(it->first) ? flecs::entity{ecs, it->first}.try_get<Script>() : nullptr;
      const bool keep = script != nullptr && script->script == it->second.script &&
                        !flecs::entity{ecs, it->first}.has<world::Disabled>();
      it = keep ? std::next(it) : m_instances.erase(it);
    }
    std::unordered_set<core::Uuid> seen;
    for (const auto &[id, uuid] : wanted) {
      if (uuid.isNil()) {
        continue;
      }
      if (seen.insert(uuid).second) {
        refresh(uuid);
      }
      const auto found = m_classes.find(uuid);
      if (found != m_classes.end() && found->second.table.valid() && !m_instances.contains(id)) {
        createInstance(id, uuid, found->second);
      }
    }
    for (auto &[id, instance] : m_instances) {
      if (!instance.started && ecs.is_alive(id)) {
        instance.started = true;
        call(id, instance, "start");
      }
    }
  }

  // Loads a script the first time and again whenever the database has a newer revision; a
  // revision that fails keeps the previous class running.
  void refresh(const core::Uuid &uuid) {
    ScriptClass &script = m_classes[uuid];
    const assets::ScriptSource *source = m_assets.script(uuid);
    const assets::AssetInfo *info = m_assets.find(uuid);
    if (source == nullptr || info == nullptr) {
      if (!script.missing) {
        SONNET_LOG_ERROR("script {} is not a script asset of the project", uuid.toString());
        script.missing = true;
      }
      return;
    }
    script.missing = false;
    if (script.revision == source->revision) {
      return;
    }
    script.revision = source->revision;
    script.path = info->source.generic_string();

    sol::load_result chunk = m_lua.load(source->code, "@" + script.path, sol::load_mode::text);
    if (!chunk.valid()) {
      const sol::error error = chunk.get<sol::error>();
      report(script.path, error.what());
      return;
    }
    sol::protected_function function = chunk.get<sol::protected_function>();
    // Each script has globals of its own over the shared ones, so two scripts never clobber each
    // other's top-level names.
    sol::environment environment{m_lua, sol::create, m_lua.globals()};
    sol::set_environment(environment, function);
    const sol::protected_function_result result = function();
    if (!result.valid()) {
      const sol::error error = result.get<sol::error>();
      report(script.path, error.what());
      return;
    }
    const sol::object value = result.get<sol::object>();
    if (value.get_type() != sol::type::table) {
      SONNET_LOG_ERROR("{}: a script has to return its table, as in \"return Script\"", script.path);
      return;
    }
    const bool reload = script.table.valid();
    script.table = value.as<sol::table>();
    script.environment = std::move(environment);
    if (reload) {
      for (auto &[id, instance] : m_instances) {
        if (instance.script == uuid) {
          instance.metatable["__index"] = script.table;
          instance.failed = false;
        }
      }
      SONNET_LOG_INFO("reloaded {}", info->source.filename().string());
    }
  }

  void createInstance(flecs::entity_t id, const core::Uuid &uuid, const ScriptClass &script) {
    Instance instance;
    instance.script = uuid;
    instance.self = m_lua.create_table();
    instance.self["entity"] = LuaEntity{id};
    instance.metatable = m_lua.create_table_with("__index", script.table);
    instance.self[sol::metatable_key] = instance.metatable;
    m_instances.emplace(id, std::move(instance));
  }

  template <typename... Args> void call(flecs::entity_t id, Instance &instance, const char *hook, Args &&...args) {
    if (instance.failed) {
      return;
    }
    const sol::object function = instance.self.get<sol::object>(hook);
    if (function.get_type() != sol::type::function) {
      return;
    }
    const sol::protected_function protectedFunction = function.as<sol::protected_function>();
    const sol::protected_function_result result = protectedFunction(instance.self, std::forward<Args>(args)...);
    if (!result.valid()) {
      instance.failed = true;
      const sol::error error = result.get<sol::error>();
      // The note goes after the message and before the stack traceback sol appends.
      std::string message = error.what();
      const std::string note = std::format(" (the script stops on \"{}\" until it changes)", nameOf(id));
      const std::size_t traceback = message.find("\nstack traceback:");
      message.insert(traceback != std::string::npos ? traceback : message.size(), note);
      const auto script = m_classes.find(instance.script);
      report(script != m_classes.end() ? script->second.path : std::string{}, message);
    }
  }

  // An error with the script's file and line as its location when the message starts with them.
  static void report(const std::string &path, const std::string &message) {
    if (const auto located = locate(message, path)) {
      const spdlog::source_loc location{path.c_str(), located->first, "lua"};
      SONNET_LOG_LOCATED(spdlog::level::err, location, "{}", located->second);
    } else {
      SONNET_LOG_ERROR("{}", message);
    }
  }

  std::string nameOf(flecs::entity_t id) const {
    const flecs::entity entity{m_world.ecs(), id};
    const world::Name *name = m_world.ecs().is_alive(id) ? entity.try_get<world::Name>() : nullptr;
    return name != nullptr ? name->value : std::format("entity {}", id);
  }

  // ---- Bindings ----

  flecs::entity alive(lua_State *state, const LuaEntity &entity) const {
    if (!m_world.ecs().is_alive(entity.id)) {
      raise(state, "the entity no longer exists");
    }
    return flecs::entity{m_world.ecs(), entity.id};
  }

  const world::ComponentInfo &component(lua_State *state, std::string_view name) const {
    const world::ComponentInfo *info = m_world.findComponent(name);
    if (info == nullptr) {
      raise(state, std::format("there is no component named \"{}\"", name));
    }
    return *info;
  }

  glm::vec3 toVec3(lua_State *state, const sol::object &value, std::string_view what) const {
    if (value.get_type() != sol::type::table) {
      raise(state, std::format("{}: expected a vec3", what));
    }
    const sol::table table = value.as<sol::table>();
    return {table.get_or("x", 0.0f), table.get_or("y", 0.0f), table.get_or("z", 0.0f)};
  }

  sol::table makeVec3(glm::vec3 v) {
    sol::table table = m_lua.create_table_with("x", v.x, "y", v.y, "z", v.z);
    table[sol::metatable_key] = m_types.vec3Metatable;
    return table;
  }

  sol::object entityObject(flecs::entity entity) {
    return entity ? sol::make_object(m_lua, LuaEntity{entity.id()}) : sol::make_object(m_lua, sol::lua_nil);
  }

  void bindLog() {
    const auto log = [](spdlog::level::level_enum level) {
      return [level](sol::this_state state, sol::variadic_args args) {
        lua_State *lua = state;
        std::string message;
        for (const auto &argument : args) {
          std::size_t length = 0;
          const char *text = luaL_tolstring(lua, argument.stack_index(), &length);
          if (!message.empty()) {
            message += ' ';
          }
          message.append(text, length);
          lua_pop(lua, 1);
        }
        // The calling script's file and line are the record's location (docs/conventions.md).
        lua_Debug frame{};
        if (lua_getstack(lua, 1, &frame) != 0 && lua_getinfo(lua, "Sl", &frame) != 0 && frame.source != nullptr &&
            frame.source[0] == '@') {
          const spdlog::source_loc location{frame.source + 1, frame.currentline, "lua"};
          SONNET_LOG_LOCATED(level, location, "{}", message);
        } else {
          const spdlog::source_loc location{frame.short_src, frame.currentline, "lua"};
          SONNET_LOG_LOCATED(level, location, "{}", message);
        }
      };
    };
    sol::table table = m_lua.create_named_table("log");
    table.set_function("debug", log(spdlog::level::debug));
    table.set_function("info", log(spdlog::level::info));
    table.set_function("warn", log(spdlog::level::warn));
    table.set_function("error", log(spdlog::level::err));
    m_lua.set_function("print", log(spdlog::level::info));
  }

  void bindEntity() {
    m_lua.new_usertype<LuaEntity>(
        "Entity", sol::no_constructor, "isValid",
        [this](const LuaEntity &entity) { return m_world.ecs().is_alive(entity.id); }, "name",
        [this](const LuaEntity &entity, sol::this_state state) {
          const world::Name *name = alive(state, entity).try_get<world::Name>();
          return name != nullptr ? name->value : std::string{};
        },
        "uuid",
        [this](const LuaEntity &entity, sol::this_state state) {
          return m_world.uuidOf(alive(state, entity)).toString();
        },
        "get",
        [this](const LuaEntity &entity, std::string_view name, sol::this_state state) -> sol::object {
          const flecs::entity target = alive(state, entity);
          const world::ComponentInfo &info = component(state, name);
          if (!target.has(info.id)) {
            return sol::make_object(m_lua, sol::lua_nil);
          }
          if (info.tag) {
            return sol::make_object(m_lua, true);
          }
          return toLua(m_lua, m_world.ecs(), m_types, info.id, target.get(info.id));
        },
        "set",
        [this](const LuaEntity &entity, std::string_view name, const sol::object &value, sol::this_state state) {
          const flecs::entity target = alive(state, entity);
          const world::ComponentInfo &info = component(state, name);
          if (info.tag) {
            target.add(info.id);
            return;
          }
          void *data = target.ensure(info.id);
          const std::optional<std::string> error = fromLua(value, m_world.ecs(), m_types, info.id, data);
          target.modified(info.id);
          if (error) {
            raise(state, std::format("{}: {}", name, *error));
          }
        },
        "has",
        [this](const LuaEntity &entity, std::string_view name, sol::this_state state) {
          return alive(state, entity).has(component(state, name).id);
        },
        "add",
        [this](const LuaEntity &entity, std::string_view name, sol::this_state state) {
          alive(state, entity).add(component(state, name).id);
        },
        "remove",
        [this](const LuaEntity &entity, std::string_view name, sol::this_state state) {
          alive(state, entity).remove(component(state, name).id);
        },
        "parent",
        [this](const LuaEntity &entity, sol::this_state state) {
          return entityObject(m_world.parentOf(alive(state, entity)));
        },
        "children",
        [this](const LuaEntity &entity, sol::this_state state) {
          sol::table children = m_lua.create_table();
          for (const flecs::entity child : m_world.children(alive(state, entity))) {
            children.add(LuaEntity{child.id()});
          }
          return children;
        },
        "worldPosition",
        [this](const LuaEntity &entity, sol::this_state state) {
          return makeVec3(glm::vec3{world::World::worldMatrix(alive(state, entity))[3]});
        },
        "destroy",
        [this](const LuaEntity &entity) {
          if (m_world.ecs().is_alive(entity.id)) {
            m_world.destroyEntity(flecs::entity{m_world.ecs(), entity.id});
          }
        },
        sol::meta_function::equal_to, [](const LuaEntity &a, const LuaEntity &b) { return a.id == b.id; },
        sol::meta_function::to_string,
        [this](const LuaEntity &entity) { return std::format("Entity({})", nameOf(entity.id)); });
  }

  void bindWorld() {
    sol::table table = m_lua.create_named_table("world");
    table.set_function("find", [this](std::string_view text) {
      const std::optional<core::Uuid> uuid = core::Uuid::parse(text);
      return entityObject(uuid ? m_world.find(*uuid) : flecs::entity{});
    });
    table.set_function("findByName", [this](std::string_view name) {
      flecs::entity found;
      m_world.ecs().each([&](flecs::entity entity, const world::Name &entityName, const world::Identity &) {
        if (entityName.value == name && (!found || world::World::pickId(entity) < world::World::pickId(found))) {
          found = entity;
        }
      });
      return entityObject(found);
    });
    table.set_function("create", [this](std::string_view name, sol::optional<LuaEntity> parent, sol::this_state state) {
      const flecs::entity parentEntity = parent ? alive(state, *parent) : flecs::entity{};
      return entityObject(m_world.createEntity(name, parentEntity));
    });
    table.set_function("instantiate", [this](std::string_view prefab, sol::optional<std::string> name,
                                             sol::optional<LuaEntity> parent, sol::this_state state) {
      flecs::entity base;
      if (const std::optional<core::Uuid> uuid = core::Uuid::parse(prefab)) {
        base = m_world.find(*uuid);
      } else {
        for (const flecs::entity candidate : m_world.prefabs()) {
          const world::Name *candidateName = candidate.try_get<world::Name>();
          if (candidateName != nullptr && candidateName->value == prefab) {
            base = candidate;
            break;
          }
        }
      }
      if (!base || !base.has(flecs::Prefab)) {
        raise(state, std::format("there is no prefab \"{}\"", prefab));
      }
      const world::Name *baseName = base.try_get<world::Name>();
      const std::string instanceName = name ? *name : (baseName != nullptr ? baseName->value : std::string{prefab});
      const flecs::entity parentEntity = parent ? alive(state, *parent) : flecs::entity{};
      return entityObject(m_world.instantiate(base, instanceName, parentEntity));
    });
  }

  void bindInput() {
    const auto key = [](std::string_view name, sol::this_state state) {
      const std::optional<platform::Key> found = platform::keyFromName(name);
      if (!found) {
        raise(state, std::format("there is no key named \"{}\"", name));
      }
      return *found;
    };
    const auto button = [](std::string_view name, sol::this_state state) {
      const std::optional<platform::MouseButton> found = platform::mouseButtonFromName(name);
      if (!found) {
        raise(state, std::format("there is no mouse button named \"{}\"", name));
      }
      return *found;
    };
    const auto vec2 = [this](glm::vec2 v) { return m_lua.create_table_with("x", v.x, "y", v.y); };
    sol::table table = m_lua.create_named_table("input");
    table.set_function("keyDown", [this, key](std::string_view name, sol::this_state state) {
      return m_input->keyDown(key(name, state));
    });
    table.set_function("keyPressed", [this, key](std::string_view name, sol::this_state state) {
      return m_input->keyPressed(key(name, state));
    });
    table.set_function("keyReleased", [this, key](std::string_view name, sol::this_state state) {
      return m_input->keyReleased(key(name, state));
    });
    table.set_function("mouseDown", [this, button](std::string_view name, sol::this_state state) {
      return m_input->mouseDown(button(name, state));
    });
    table.set_function("mousePressed", [this, button](std::string_view name, sol::this_state state) {
      return m_input->mousePressed(button(name, state));
    });
    table.set_function("mouseReleased", [this, button](std::string_view name, sol::this_state state) {
      return m_input->mouseReleased(button(name, state));
    });
    table.set_function("mousePosition", [this, vec2] { return vec2(m_input->mousePosition()); });
    table.set_function("mouseDelta", [this, vec2] { return vec2(m_input->mouseDelta()); });
    table.set_function("wheel", [this, vec2] { return vec2(m_input->wheel()); });
  }

  void bindPhysics() {
    sol::table table = m_lua.create_named_table("physics");
    table.set_function("raycast",
                       [this](const sol::object &origin, const sol::object &direction, float maxDistance,
                              sol::optional<LuaEntity> ignore, sol::this_state state) -> sol::object {
                         const flecs::entity ignored = ignore ? alive(state, *ignore) : flecs::entity{};
                         const std::optional<physics::RaycastHit> hit =
                             m_physics->raycast(toVec3(state, origin, "origin"), toVec3(state, direction, "direction"),
                                                maxDistance, ignored);
                         if (!hit) {
                           return sol::make_object(m_lua, sol::lua_nil);
                         }
                         return m_lua.create_table_with("entity", LuaEntity{hit->entity.id()}, "point",
                                                        makeVec3(hit->point), "normal", makeVec3(hit->normal),
                                                        "distance", hit->distance);
                       });
    table.set_function("addImpulse",
                       [this](const LuaEntity &entity, const sol::object &impulse, sol::this_state state) {
                         m_physics->addImpulse(alive(state, entity), toVec3(state, impulse, "impulse"));
                       });
    table.set_function("addForce", [this](const LuaEntity &entity, const sol::object &force, sol::this_state state) {
      m_physics->addForce(alive(state, entity), toVec3(state, force, "force"));
    });
    table.set_function("linearVelocity", [this](const LuaEntity &entity, sol::this_state state) {
      return makeVec3(m_physics->linearVelocity(alive(state, entity)));
    });
    table.set_function("setLinearVelocity",
                       [this](const LuaEntity &entity, const sol::object &velocity, sol::this_state state) {
                         m_physics->setLinearVelocity(alive(state, entity), toVec3(state, velocity, "velocity"));
                       });
    table.set_function("angularVelocity", [this](const LuaEntity &entity, sol::this_state state) {
      return makeVec3(m_physics->angularVelocity(alive(state, entity)));
    });
    table.set_function("setAngularVelocity",
                       [this](const LuaEntity &entity, const sol::object &velocity, sol::this_state state) {
                         m_physics->setAngularVelocity(alive(state, entity), toVec3(state, velocity, "velocity"));
                       });
  }

  world::World &m_world;
  assets::AssetDatabase &m_assets;
  physics::IPhysicsWorld *m_physics;
  const platform::InputState *m_input;
  // Declared first so it is destroyed last: every sol reference below points into it.
  sol::state m_lua;
  LuaTypes m_types;
  std::unordered_map<core::Uuid, ScriptClass> m_classes;
  // Ordered by id, so calls happen in a stable order, close to creation order.
  std::map<flecs::entity_t, Instance> m_instances;
  flecs::query<const Script> m_scripts;
  flecs::system m_fixedSystem;
  flecs::system m_updateSystem;
};

} // namespace

std::unique_ptr<IScriptRuntime> createScriptRuntime(const ScriptDesc &desc) {
  SONNET_ASSERT(desc.world != nullptr && desc.assets != nullptr, "a script runtime needs a world and assets");
  return std::make_unique<LuaScriptRuntime>(desc);
}

} // namespace sonnet::scripting
