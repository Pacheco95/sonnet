#pragma once

#include <sonnet/scripting/Components.h>

#include <sonnet/core/Error.h>
#include <sonnet/core/Math.h>
#include <sonnet/renderer/Camera.h>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sonnet::assets {
class AssetDatabase;
}
namespace sonnet::physics {
class IPhysicsWorld;
}
namespace sonnet::platform {
class InputState;
}
namespace sonnet::world {
class World;
class AnimationSystem;
} // namespace sonnet::world

namespace sonnet::scripting {

// The view the game is drawn into, which the application updates before the world's frame: the
// camera it draws through and its size in the coordinates of the input's positions, so a script
// can turn a pointer or a touch into a ray (docs/scripting.md, "camera").
struct ScriptView {
  renderer::Camera camera;
  glm::vec2 size{0.0f, 0.0f};
};

// The kinds of value a script's class can declare as an editable property (ADR-0022). In a
// slot's JSON a number is a number, an integer an integer, a vec3 `{x, y, z}`, a color
// `{r, g, b, a}`, and an entity or an asset the canonical identity string.
enum class PropertyType : std::uint8_t {
  Number,
  Integer,
  Boolean,
  String,
  Vec3,
  Color,
  Entity,
  Asset,
};

[[nodiscard]] std::string_view toString(PropertyType type) noexcept;

// One entry of a class's `properties` table, as the inspector shows it.
struct PropertyDecl {
  std::string name;
  PropertyType type{PropertyType::Number};
  // In the JSON a slot would store; null for an entity or asset with no default.
  nlohmann::json defaultValue{};
  std::optional<double> min{};
  std::optional<double> max{};
};

// What scripts reach. The world and the assets are required; without physics, input or a view
// the `physics`, `input` and `camera` tables are absent. Everything has to outlive the runtime.
struct ScriptDesc {
  world::World *world{nullptr};
  assets::AssetDatabase *assets{nullptr};
  physics::IPhysicsWorld *physics{nullptr};
  const world::AnimationSystem *animation{nullptr}; // its events reach scripts as onAnimationEvent
  const platform::InputState *input{nullptr};
  const ScriptView *view{nullptr};
};

// Runs the Scripts components of a world (ADR-0009, ADR-0022, docs/scripting.md). In play mode
// every slot of every enabled entity gets an instance of its script's class, with the properties
// the class declares set from the slot: `start` is called once, `update(dt)` every frame in the
// Update phase and `fixedUpdate(dt)` every fixed step, after physics and after the step's
// contact and trigger events. A script that fails to load is reported once per revision; a call
// that fails is reported with the script's file and line and turns its instance off until the
// script changes, which reloads it under the running instances, keeping their state.
class IScriptRuntime {
public:
  virtual ~IScriptRuntime() = default;

  // Drops every instance and loaded script, so the next play starts from fresh script state; the
  // editor calls it when play stops.
  virtual void reset() = 0;
  // Seeds math.random, which Lua otherwise seeds differently in every process. The editor's
  // captures seed it before playing, so a scene whose scripts draw random numbers plays the same
  // run after run (docs/editor.md, "Screenshots").
  virtual void seedRandom(std::uint64_t seed) = 0;
  // Runs a chunk outside any entity with the same globals scripts see, for tests and tools.
  [[nodiscard]] virtual core::Result<void> run(std::string_view code, std::string_view chunkName) = 0;
  [[nodiscard]] virtual std::uint32_t instanceCount() const = 0;
  // The properties the script's class declares, sorted by name, loading the class if need be:
  // what the inspector draws, in edit mode too. An error when the script is missing or has not
  // loaded; a revision that fails keeps answering with the last one that loaded.
  [[nodiscard]] virtual core::Result<std::vector<PropertyDecl>> properties(const core::Uuid &script) = 0;
};

// Registers the component and the scripting systems on the world. The Lua implementation; create
// it after the physics world so fixedUpdate runs after the physics step.
[[nodiscard]] std::unique_ptr<IScriptRuntime> createScriptRuntime(const ScriptDesc &desc);

} // namespace sonnet::scripting
