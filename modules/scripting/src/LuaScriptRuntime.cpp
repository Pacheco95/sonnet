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

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
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

std::string_view toString(PropertyType type) noexcept {
  switch (type) {
  case PropertyType::Number:
    return "number";
  case PropertyType::Integer:
    return "integer";
  case PropertyType::Boolean:
    return "boolean";
  case PropertyType::String:
    return "string";
  case PropertyType::Vec3:
    return "vec3";
  case PropertyType::Color:
    return "color";
  case PropertyType::Entity:
    return "entity";
  case PropertyType::Asset:
    return "asset";
  }
  return "";
}

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
      : m_world(*desc.world), m_assets(*desc.assets), m_physics(desc.physics), m_input(desc.input), m_view(desc.view) {
    registerComponents(m_world);
    // No io, os, package or debug: scripts reach the engine through its tables only, the same on
    // every platform the player runs on (ADR-0009).
    m_lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table, sol::lib::utf8,
                         sol::lib::coroutine);
    m_lua["dofile"] = sol::lua_nil;
    m_lua["loadfile"] = sol::lua_nil;
    // The only way into another file: a script asset by name or path (ADR-0022).
    m_lua.set_function("require",
                       [this](std::string_view name, sol::this_state state) { return requireModule(name, state); });

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
    if (m_view != nullptr) {
      bindCamera();
    }

    m_scripts = ecs.query_builder<const Scripts>("ScriptSlots").without<world::Disabled>().build();
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
    m_modules.clear();
    m_resolved.clear();
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

  core::Result<std::vector<PropertyDecl>> properties(const core::Uuid &script) override {
    if (script.isNil()) {
      return std::unexpected(core::Error{"no script", core::ErrorCategory::Script});
    }
    refresh(script);
    const auto found = m_classes.find(script);
    if (found == m_classes.end() || !found->second.table.valid()) {
      return std::unexpected(
          core::Error{found != m_classes.end() && !found->second.error.empty()
                          ? found->second.error
                          : std::format("{} is not a script asset of the project", script.toString()),
                      core::ErrorCategory::Script});
    }
    return found->second.declarations;
  }

private:
  using Required = std::unordered_map<core::Uuid, std::uint64_t>; // script assets and the revisions seen

  struct ScriptClass {
    std::uint64_t revision{0}; // of the source last tried, loaded or not
    std::string path;
    sol::environment environment;
    sol::table table; // invalid until a revision loads
    bool missing{false};
    std::vector<PropertyDecl> declarations;
    std::string error;        // why the latest revision did not load; empty when it did
    Required required;        // modules the class asked for, with the revisions it saw
    std::uint64_t checked{0}; // the sync that last looked at the source
  };
  // A script file loaded by `require`: its value, shared by whoever asks.
  struct Module {
    std::uint64_t revision{0};
    sol::object value;
    Required required;
  };
  struct InstanceKey {
    flecs::entity_t entity{0};
    std::uint32_t slot{0};
    auto operator<=>(const InstanceKey &) const = default;
  };
  struct Instance {
    core::Uuid script;
    sol::table self;
    sol::table metatable;   // its __index is the class, swapped on reload
    std::string properties; // the slot's JSON as last applied to `self`
    std::uint64_t seen{0};  // the sync that last found its slot
    bool started{false};
    bool failed{false};
  };
  using Instances = std::map<InstanceKey, Instance>;

  // ---- Frames ----

  void frame(const char *hook, float dt) {
    SONNET_ZONE();
    flecs::world &ecs = m_world.ecs();
    ecs.defer_suspend();
    sync();
    if (std::string_view{hook} == "fixedUpdate") {
      deliverEvents();
    }
    for (auto &[key, instance] : m_instances) {
      // A script earlier in this frame may have destroyed the entity.
      if (instance.started && ecs.is_alive(key.entity)) {
        call(key, instance, hook, dt);
      }
    }
    ecs.defer_resume();
  }

  // Instances for the slots of the enabled entities, loaded or reloaded classes, properties
  // that changed under a running instance, and start for the new instances.
  void sync() {
    flecs::world &ecs = m_world.ecs();
    ++m_generation;
    m_scripts.each([&](flecs::entity entity, const Scripts &scripts) {
      for (std::size_t index = 0; index < scripts.slots.size(); ++index) {
        const ScriptSlot &slot = scripts.slots[index];
        if (slot.script.isNil()) {
          continue;
        }
        const InstanceKey key{entity.id(), static_cast<std::uint32_t>(index)};
        auto found = m_instances.find(key);
        if (found != m_instances.end() && found->second.script != slot.script) {
          m_instances.erase(found);
          found = m_instances.end();
        }
        const ScriptClass *script = loadedClass(slot.script);
        if (found == m_instances.end()) {
          if (script == nullptr) {
            continue;
          }
          found = createInstance(key, slot, *script);
        } else if (script != nullptr && found->second.properties != slot.properties) {
          reapplyProperties(key, found->second, *script, slot.properties);
        }
        found->second.seen = m_generation;
      }
    });
    for (auto it = m_instances.begin(); it != m_instances.end();) {
      it = it->second.seen == m_generation && ecs.is_alive(it->first.entity) ? std::next(it) : m_instances.erase(it);
    }
    for (auto &[key, instance] : m_instances) {
      if (!instance.started && ecs.is_alive(key.entity)) {
        instance.started = true;
        call(key, instance, "start");
      }
    }
  }

  // The class of a script, looked at once per sync, or null while it has not loaded.
  const ScriptClass *loadedClass(const core::Uuid &uuid) {
    ScriptClass &script = m_classes[uuid];
    if (script.checked != m_generation) {
      script.checked = m_generation;
      refresh(uuid);
    }
    return script.table.valid() ? &script : nullptr;
  }

  // ---- Events ----

  // The step's contact and trigger events, to the instances of both entities of each, in slot
  // order, before this fixed step's hooks. An entity destroyed or disabled since the step, or by
  // an earlier hook, gets nothing; `other` may be one that is gone.
  void deliverEvents() {
    if (m_physics == nullptr) {
      return;
    }
    for (const physics::ContactEvent &event : m_physics->events()) {
      deliver(event, event.first, event.second, false);
      deliver(event, event.second, event.first, true);
    }
  }

  void deliver(const physics::ContactEvent &event, flecs::entity self, flecs::entity other, bool towardsFirst) {
    using Kind = physics::ContactEventKind;
    const char *hook = event.kind == Kind::ContactBegin   ? "onContactBegin"
                       : event.kind == Kind::ContactEnd   ? "onContactEnd"
                       : event.kind == Kind::TriggerEnter ? "onTriggerEnter"
                                                          : "onTriggerExit";
    flecs::world &ecs = m_world.ecs();
    for (auto it = m_instances.lower_bound({self.id(), 0}); it != m_instances.end() && it->first.entity == self.id();
         ++it) {
      if (!it->second.started || !ecs.is_alive(self.id()) || flecs::entity{ecs, self.id()}.has<world::Disabled>()) {
        continue;
      }
      if (event.kind == Kind::ContactBegin) {
        // The normal points from the instance's entity towards the other.
        const sol::table contact = m_lua.create_table_with("point", makeVec3(event.point), "normal",
                                                           makeVec3(towardsFirst ? -event.normal : event.normal));
        call(it->first, it->second, hook, LuaEntity{other.id()}, contact);
      } else {
        call(it->first, it->second, hook, LuaEntity{other.id()});
      }
    }
  }

  // ---- Classes ----

  // Loads a script the first time and again whenever the database has a newer revision of it or
  // of a module it required; a revision that fails keeps the previous class running.
  void refresh(const core::Uuid &uuid) {
    ScriptClass &script = m_classes[uuid];
    const assets::ScriptSource *source = m_assets.script(uuid);
    const assets::AssetInfo *info = m_assets.find(uuid);
    if (source == nullptr || info == nullptr) {
      if (!script.missing) {
        SONNET_LOG_ERROR("script {} is not a script asset of the project", uuid.toString());
        script.missing = true;
        script.error = std::format("{} is not a script asset of the project", uuid.toString());
      }
      return;
    }
    script.missing = false;
    if (script.revision == source->revision && !stale(script.required)) {
      return;
    }
    script.revision = source->revision;
    script.path = info->source.generic_string();

    Required required;
    const auto fail = [&](const std::string &message) {
      script.required = std::move(required);
      script.error = message;
      report(script.path, message);
    };
    sol::load_result chunk = m_lua.load(source->code, "@" + script.path, sol::load_mode::text);
    if (!chunk.valid()) {
      fail(chunk.get<sol::error>().what());
      return;
    }
    sol::protected_function function = chunk.get<sol::protected_function>();
    // Each script has globals of its own over the shared ones, so two scripts never clobber each
    // other's top-level names.
    sol::environment environment{m_lua, sol::create, m_lua.globals()};
    sol::set_environment(environment, function);
    m_loading.push_back(uuid);
    m_requirers.push_back(&required);
    const sol::protected_function_result result = function();
    m_requirers.pop_back();
    m_loading.pop_back();
    if (!result.valid()) {
      fail(result.get<sol::error>().what());
      return;
    }
    const sol::object value = result.get<sol::object>();
    if (value.get_type() != sol::type::table) {
      SONNET_LOG_ERROR("{}: a script has to return its table, as in \"return Script\"", script.path);
      script.required = std::move(required);
      script.error = "the script does not return a table";
      return;
    }
    const bool reload = script.table.valid();
    script.table = value.as<sol::table>();
    script.environment = std::move(environment);
    script.required = std::move(required);
    script.error.clear();
    script.declarations = readDeclarations(script.table, script.path);
    if (reload) {
      for (auto &[key, instance] : m_instances) {
        if (instance.script == uuid) {
          instance.metatable["__index"] = script.table;
          instance.failed = false;
        }
      }
      SONNET_LOG_INFO("reloaded {}", info->source.filename().string());
    }
  }

  // Whether a module a script asked for has a revision other than the one it saw.
  bool stale(const Required &required) {
    return std::ranges::any_of(required, [&](const auto &entry) {
      const assets::ScriptSource *source = m_assets.script(entry.first);
      return source == nullptr || source->revision != entry.second;
    });
  }

  Instances::iterator createInstance(const InstanceKey &key, const ScriptSlot &slot, const ScriptClass &script) {
    Instance instance;
    instance.script = slot.script;
    instance.self = m_lua.create_table();
    instance.self["entity"] = LuaEntity{key.entity};
    instance.self["slot"] = static_cast<lua_Integer>(key.slot) + 1; // Lua counts from one
    instance.metatable = m_lua.create_table_with("__index", script.table);
    instance.self[sol::metatable_key] = instance.metatable;
    applyProperties(key, instance, script, slot.properties);
    instance.properties = slot.properties;
    return m_instances.emplace(key, std::move(instance)).first;
  }

  template <typename... Args> void call(const InstanceKey &key, Instance &instance, const char *hook, Args &&...args) {
    if (instance.failed) {
      return;
    }
    const sol::object function = instance.self.get<sol::object>(hook);
    if (function.get_type() != sol::type::function) {
      return;
    }
    const sol::protected_function protectedFunction = function.as<sol::protected_function>();
    // Whatever the hook requires belongs to its class, so changing that file reloads the class.
    const auto owner = m_classes.find(instance.script);
    if (owner != m_classes.end()) {
      m_requirers.push_back(&owner->second.required);
    }
    const sol::protected_function_result result = protectedFunction(instance.self, std::forward<Args>(args)...);
    if (owner != m_classes.end()) {
      m_requirers.pop_back();
    }
    if (!result.valid()) {
      instance.failed = true;
      const sol::error error = result.get<sol::error>();
      // The note goes after the message and before the stack traceback sol appends.
      std::string message = error.what();
      const std::string note = std::format(" (the script stops on \"{}\" until it changes)", nameOf(key.entity));
      const std::size_t traceback = message.find("\nstack traceback:");
      message.insert(traceback != std::string::npos ? traceback : message.size(), note);
      report(owner != m_classes.end() ? owner->second.path : std::string{}, message);
    }
  }

  // ---- Properties ----

  // The class's `properties` table as declarations sorted by name. A malformed entry is
  // reported and left out.
  std::vector<PropertyDecl> readDeclarations(const sol::table &script, const std::string &path) {
    std::vector<PropertyDecl> declarations;
    const sol::object table = script.get<sol::object>("properties");
    if (table.get_type() == sol::type::lua_nil) {
      return declarations;
    }
    if (table.get_type() != sol::type::table) {
      SONNET_LOG_ERROR("{}: properties has to be a table of name = {{ type = ..., default = ... }}", path);
      return declarations;
    }
    for (const auto &[key, value] : table.as<sol::table>()) {
      if (key.get_type() != sol::type::string || value.get_type() != sol::type::table) {
        SONNET_LOG_ERROR("{}: properties has to be a table of name = {{ type = ..., default = ... }}", path);
        continue;
      }
      const std::string name = key.as<std::string>();
      const sol::table entry = value.as<sol::table>();
      const sol::object typeName = entry.get<sol::object>("type");
      std::optional<PropertyType> type;
      if (typeName.get_type() == sol::type::string) {
        for (const PropertyType candidate :
             {PropertyType::Number, PropertyType::Integer, PropertyType::Boolean, PropertyType::String,
              PropertyType::Vec3, PropertyType::Color, PropertyType::Entity, PropertyType::Asset}) {
          if (toString(candidate) == typeName.as<std::string_view>()) {
            type = candidate;
          }
        }
      }
      if (!type) {
        SONNET_LOG_ERROR("{}: property \"{}\" needs a type: number, integer, boolean, string, vec3, color, entity or "
                         "asset",
                         path, name);
        continue;
      }
      PropertyDecl declaration{.name = name, .type = *type, .defaultValue = zeroValue(*type)};
      const sol::object defaultValue = entry.get<sol::object>("default");
      if (defaultValue.get_type() != sol::type::lua_nil) {
        std::optional<nlohmann::json> json = jsonFromLua(*type, defaultValue);
        if (!json) {
          SONNET_LOG_ERROR("{}: the default of property \"{}\" is not a {}", path, name, toString(*type));
          continue;
        }
        declaration.defaultValue = std::move(*json);
      }
      const sol::object minimum = entry.get<sol::object>("min");
      if (minimum.get_type() == sol::type::number) {
        declaration.min = minimum.as<double>();
      }
      const sol::object maximum = entry.get<sol::object>("max");
      if (maximum.get_type() == sol::type::number) {
        declaration.max = maximum.as<double>();
      }
      declarations.push_back(std::move(declaration));
    }
    std::ranges::sort(declarations, {}, &PropertyDecl::name);
    return declarations;
  }

  static nlohmann::json zeroValue(PropertyType type) {
    switch (type) {
    case PropertyType::Number:
      return 0.0;
    case PropertyType::Integer:
      return 0;
    case PropertyType::Boolean:
      return false;
    case PropertyType::String:
      return std::string{};
    case PropertyType::Vec3:
      return {{"x", 0.0}, {"y", 0.0}, {"z", 0.0}};
    case PropertyType::Color:
      return {{"r", 1.0}, {"g", 1.0}, {"b", 1.0}, {"a", 1.0}};
    case PropertyType::Entity:
    case PropertyType::Asset:
      break;
    }
    return nullptr;
  }

  static std::optional<nlohmann::json> some(nlohmann::json value) {
    return std::optional<nlohmann::json>{std::in_place, std::move(value)};
  }

  // A Lua value as the JSON a slot stores for that kind of property; none when it is another kind.
  static std::optional<nlohmann::json> jsonFromLua(PropertyType type, const sol::object &value) {
    const auto number = [](const sol::table &table, const char *name, double fallback) {
      const sol::object field = table.get<sol::object>(name);
      return field.get_type() == sol::type::number ? field.as<double>() : fallback;
    };
    switch (type) {
    case PropertyType::Number:
      if (value.get_type() == sol::type::number) {
        return some(nlohmann::json(value.as<double>()));
      }
      return std::nullopt;
    case PropertyType::Integer:
      if (value.get_type() == sol::type::number && value.as<double>() == std::floor(value.as<double>())) {
        return some(nlohmann::json(static_cast<std::int64_t>(value.as<double>())));
      }
      return std::nullopt;
    case PropertyType::Boolean:
      if (value.get_type() == sol::type::boolean) {
        return some(nlohmann::json(value.as<bool>()));
      }
      return std::nullopt;
    case PropertyType::String:
      if (value.get_type() == sol::type::string) {
        return some(nlohmann::json(value.as<std::string>()));
      }
      return std::nullopt;
    case PropertyType::Vec3:
      if (value.get_type() == sol::type::table) {
        const sol::table table = value.as<sol::table>();
        return some(nlohmann::json{
            {"x", number(table, "x", 0.0)}, {"y", number(table, "y", 0.0)}, {"z", number(table, "z", 0.0)}});
      }
      return std::nullopt;
    case PropertyType::Color:
      if (value.get_type() == sol::type::table) {
        const sol::table table = value.as<sol::table>();
        return some(nlohmann::json{{"r", number(table, "r", 1.0)},
                                   {"g", number(table, "g", 1.0)},
                                   {"b", number(table, "b", 1.0)},
                                   {"a", number(table, "a", 1.0)}});
      }
      return std::nullopt;
    case PropertyType::Entity:
    case PropertyType::Asset:
      if (value.get_type() == sol::type::string && core::Uuid::parse(value.as<std::string_view>())) {
        return some(nlohmann::json(value.as<std::string>()));
      }
      return std::nullopt;
    }
    return std::nullopt;
  }

  // The Lua value of a property from its JSON; none when the JSON is another kind.
  std::optional<sol::object> propertyToLua(const PropertyDecl &declaration, const nlohmann::json &json) {
    const auto real = [&](const char *name) -> std::optional<double> {
      return json.is_object() && json.contains(name) && json[name].is_number()
                 ? std::optional<double>{json[name].get<double>()}
                 : std::nullopt;
    };
    switch (declaration.type) {
    case PropertyType::Number:
      if (json.is_number()) {
        return sol::make_object(m_lua, json.get<double>());
      }
      break;
    case PropertyType::Integer:
      if (json.is_number_integer()) {
        return sol::make_object(m_lua, static_cast<lua_Integer>(json.get<std::int64_t>()));
      }
      break;
    case PropertyType::Boolean:
      if (json.is_boolean()) {
        return sol::make_object(m_lua, json.get<bool>());
      }
      break;
    case PropertyType::String:
      if (json.is_string()) {
        return sol::make_object(m_lua, json.get<std::string>());
      }
      break;
    case PropertyType::Vec3:
      if (real("x") && real("y") && real("z")) {
        return sol::make_object(m_lua, makeVec3({static_cast<float>(*real("x")), static_cast<float>(*real("y")),
                                                 static_cast<float>(*real("z"))}));
      }
      break;
    case PropertyType::Color:
      if (real("r") && real("g") && real("b")) {
        return sol::make_object(m_lua, m_lua.create_table_with("r", *real("r"), "g", *real("g"), "b", *real("b"), "a",
                                                               real("a").value_or(1.0)));
      }
      break;
    case PropertyType::Entity:
    case PropertyType::Asset:
      if (json.is_null()) {
        return sol::make_object(m_lua, sol::lua_nil);
      }
      if (json.is_string()) {
        const std::optional<core::Uuid> uuid = core::Uuid::parse(json.get<std::string>());
        if (!uuid) {
          break;
        }
        if (declaration.type == PropertyType::Asset) {
          return sol::make_object(m_lua, uuid->toString());
        }
        return entityObject(m_world.find(*uuid)); // nil while no such entity exists
      }
      break;
    }
    return std::nullopt;
  }

  static nlohmann::json parseProperties(const std::string &text) {
    if (text.empty()) {
      return nlohmann::json::object();
    }
    nlohmann::json json = nlohmann::json::parse(text, nullptr, false);
    return json.is_object() ? json : nlohmann::json::object();
  }

  std::string describeSlot(const InstanceKey &key, const ScriptClass &script) const {
    return std::format("{}: slot {} of \"{}\"", script.path, key.slot + 1, nameOf(key.entity));
  }

  // Sets the instance's fields from the class's defaults overlaid with the slot's values. A name
  // the class does not declare is kept in the slot and reported; a value of the wrong kind falls
  // back to the default.
  void applyProperties(const InstanceKey &key, Instance &instance, const ScriptClass &script, const std::string &text) {
    const nlohmann::json values = parseProperties(text);
    if (!text.empty() && !values.is_object()) {
      SONNET_LOG_WARN("{}: its properties are not a JSON object and are ignored", describeSlot(key, script));
    }
    for (const PropertyDecl &declaration : script.declarations) {
      std::optional<sol::object> value;
      if (values.contains(declaration.name)) {
        value = propertyToLua(declaration, values[declaration.name]);
        if (!value) {
          SONNET_LOG_WARN("{}: property \"{}\" does not hold a valid {}, using the default", describeSlot(key, script),
                          declaration.name, toString(declaration.type));
        }
      }
      if (!value) {
        value = propertyToLua(declaration, declaration.defaultValue);
      }
      if (value && value->get_type() != sol::type::lua_nil) {
        instance.self[declaration.name] = *value;
      }
    }
    for (const auto &[name, value] : values.items()) {
      if (std::ranges::none_of(script.declarations, [&](const PropertyDecl &d) { return d.name == name; })) {
        SONNET_LOG_WARN("{}: property \"{}\" is not declared by the script, keeping it in the scene",
                        describeSlot(key, script), name);
      }
    }
  }

  // The slot's JSON changed while the instance runs: only the properties whose value differs
  // are set again, so what a script did to the others stays.
  void reapplyProperties(const InstanceKey &key, Instance &instance, const ScriptClass &script,
                         const std::string &text) {
    const nlohmann::json before = parseProperties(instance.properties);
    const nlohmann::json after = parseProperties(text);
    for (const PropertyDecl &declaration : script.declarations) {
      const auto valueIn = [&](const nlohmann::json &values) {
        return values.contains(declaration.name) ? values[declaration.name] : declaration.defaultValue;
      };
      const nlohmann::json now = valueIn(after);
      if (now == valueIn(before)) {
        continue;
      }
      std::optional<sol::object> value = propertyToLua(declaration, now);
      if (!value) {
        SONNET_LOG_WARN("{}: property \"{}\" does not hold a valid {}, using the default", describeSlot(key, script),
                        declaration.name, toString(declaration.type));
        value = propertyToLua(declaration, declaration.defaultValue);
      }
      if (value) {
        instance.self[declaration.name] = *value;
      }
    }
    instance.properties = text;
  }

  // ---- require ----

  // "scripts/util", "scripts/util.lua", "util" or the path under an asset root: the script asset
  // the name stands for. A path beats a bare name, and a name two scripts share is an error.
  const assets::AssetInfo *resolveScript(std::string_view name, std::string &error) {
    std::string wanted{name};
    if (wanted.starts_with("./")) {
      wanted.erase(0, 2);
    }
    if (wanted.ends_with(".lua")) {
      wanted.resize(wanted.size() - 4);
    }
    if (const auto cached = m_resolved.find(wanted); cached != m_resolved.end()) {
      if (const assets::AssetInfo *info = m_assets.find(cached->second)) {
        return info;
      }
    }
    std::vector<const assets::AssetInfo *> byPath;
    std::vector<const assets::AssetInfo *> byName;
    for (const assets::AssetInfo *info : m_assets.assets(assets::AssetType::Script)) {
      std::string path = relativePath(*info);
      if (path.ends_with(".lua")) {
        path.resize(path.size() - 4);
      }
      bool matches = path == wanted;
      for (const std::string &root : m_assets.roots()) {
        matches = matches || path == std::format("{}/{}", root, wanted);
      }
      if (matches) {
        byPath.push_back(info);
      } else if (info->name == wanted) {
        byName.push_back(info);
      }
    }
    const std::vector<const assets::AssetInfo *> &found = byPath.empty() ? byName : byPath;
    if (found.empty()) {
      error = std::format("there is no script \"{}\" in the project", name);
      return nullptr;
    }
    if (found.size() > 1) {
      error = std::format("\"{}\" is ambiguous: {} and {} both match, use the path from the project's root", name,
                          relativePath(*found[0]), relativePath(*found[1]));
      return nullptr;
    }
    m_resolved[wanted] = found.front()->uuid;
    return found.front();
  }

  std::string relativePath(const assets::AssetInfo &info) const {
    std::filesystem::path relative = info.source;
    if (!m_assets.projectRoot().empty()) {
      const std::filesystem::path candidate = info.source.lexically_relative(m_assets.projectRoot());
      if (!candidate.empty() && !candidate.generic_string().starts_with("..")) {
        relative = candidate;
      }
    }
    return relative.generic_string();
  }

  // What `require` returns: the file's value, loaded once however many scripts ask and again
  // when its file or one it required changes. It is recorded as a dependency of whatever is
  // loading or running, so a script is reloaded when a module of it changes.
  sol::object requireModule(std::string_view name, lua_State *state) {
    std::string error;
    const assets::AssetInfo *info = resolveScript(name, error);
    if (info == nullptr) {
      raise(state, error);
    }
    const core::Uuid uuid = info->uuid;
    const assets::ScriptSource *source = m_assets.script(uuid);
    if (source == nullptr) {
      raise(state, std::format("\"{}\" cannot be read", name));
    }
    if (const auto start = std::ranges::find(m_loading, uuid); start != m_loading.end()) {
      std::string chain;
      for (auto it = start; it != m_loading.end(); ++it) {
        chain += std::format("{} -> ", relativePath(*m_assets.find(*it)));
      }
      raise(state, std::format("require cycle: {}{}", chain, relativePath(*info)));
    }
    if (!m_requirers.empty()) {
      (*m_requirers.back())[uuid] = source->revision;
    }
    Module &module = m_modules[uuid];
    if (!module.value.valid() || module.revision != source->revision || stale(module.required)) {
      const std::string path = info->source.generic_string();
      Required required;
      sol::load_result chunk = m_lua.load(source->code, "@" + path, sol::load_mode::text);
      if (!chunk.valid()) {
        raise(state, chunk.get<sol::error>().what());
      }
      sol::protected_function function = chunk.get<sol::protected_function>();
      const sol::environment environment{m_lua, sol::create, m_lua.globals()};
      sol::set_environment(environment, function);
      m_loading.push_back(uuid);
      m_requirers.push_back(&required);
      const sol::protected_function_result result = function();
      m_requirers.pop_back();
      m_loading.pop_back();
      if (!m_requirers.empty()) {
        m_requirers.back()->insert(required.begin(), required.end());
      }
      if (!result.valid()) {
        module.value = sol::object{};
        raise(state, result.get<sol::error>().what());
      }
      sol::object value = result.get<sol::object>();
      module.value = value.get_type() == sol::type::lua_nil ? sol::make_object(m_lua, true) : std::move(value);
      module.revision = source->revision;
      module.required = std::move(required);
    } else if (!m_requirers.empty()) {
      m_requirers.back()->insert(module.required.begin(), module.required.end());
    }
    return module.value;
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
    table.set_function("touches", [this, vec2] {
      sol::table touches = m_lua.create_table(static_cast<int>(m_input->touches().size()), 0);
      int index = 1;
      for (const platform::Touch &touch : m_input->touches()) {
        // Lua's integers are signed; an id only has to stay the same while its finger is down.
        touches[index++] = m_lua.create_table_with("id", static_cast<std::int64_t>(touch.id), "position",
                                                   vec2(touch.position), "delta", vec2(touch.delta));
      }
      return touches;
    });
  }

  void bindCamera() {
    sol::table table = m_lua.create_named_table("camera");
    table.set_function("ray", [this](const sol::object &point, sol::this_state state) {
      if (point.get_type() != sol::type::table) {
        raise(state, "point: expected a table {x, y}");
      }
      const sol::table xy = point.as<sol::table>();
      const glm::vec2 size = glm::max(m_view->size, glm::vec2{1.0f});
      const glm::vec2 fraction = glm::vec2{xy.get_or("x", 0.0f), xy.get_or("y", 0.0f)} / size;
      const renderer::Camera &camera = m_view->camera;
      return m_lua.create_table_with("origin", makeVec3(camera.position), "direction",
                                     makeVec3(camera.rayDirection(fraction, size.x / size.y)));
    });
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
  const ScriptView *m_view;
  // Declared first so it is destroyed last: every sol reference below points into it.
  sol::state m_lua;
  LuaTypes m_types;
  std::unordered_map<core::Uuid, ScriptClass> m_classes;
  std::unordered_map<core::Uuid, Module> m_modules;
  std::unordered_map<std::string, core::Uuid> m_resolved; // require's names, until the asset goes
  // The scripts and modules being loaded, outermost first, for the cycle check; and where a
  // `require` records what it returned, innermost last.
  std::vector<core::Uuid> m_loading;
  std::vector<Required *> m_requirers;
  std::uint64_t m_generation{0};
  // Ordered by entity id and slot, so calls happen in a stable order, close to creation order,
  // with an entity's slots in order.
  Instances m_instances;
  flecs::query<const Scripts> m_scripts;
  flecs::system m_fixedSystem;
  flecs::system m_updateSystem;
};

} // namespace

std::unique_ptr<IScriptRuntime> createScriptRuntime(const ScriptDesc &desc) {
  SONNET_ASSERT(desc.world != nullptr && desc.assets != nullptr, "a script runtime needs a world and assets");
  return std::make_unique<LuaScriptRuntime>(desc);
}

} // namespace sonnet::scripting
