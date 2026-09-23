#pragma once

#include <sonnet/scripting/Components.h>

#include <sonnet/core/Error.h>

#include <cstdint>
#include <memory>
#include <string_view>

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
}

namespace sonnet::scripting {

// What scripts reach. The world and the assets are required; without physics or input the
// `physics` and `input` tables are absent. Everything has to outlive the runtime.
struct ScriptDesc {
  world::World *world{nullptr};
  assets::AssetDatabase *assets{nullptr};
  physics::IPhysicsWorld *physics{nullptr};
  const platform::InputState *input{nullptr};
};

// Runs the Script components of a world (ADR-0009, docs/scripting.md). In play mode every
// enabled entity with a Script gets an instance of its script's class: `start` is called once,
// `update(dt)` every frame in the Update phase and `fixedUpdate(dt)` every fixed step, after
// physics. A script that fails to load is reported once per revision; a call that fails is
// reported with the script's file and line and turns its instance off until the script changes,
// which reloads it under the running instances, keeping their state.
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
};

// Registers the component and the scripting systems on the world. The Lua implementation; create
// it after the physics world so fixedUpdate runs after the physics step.
[[nodiscard]] std::unique_ptr<IScriptRuntime> createScriptRuntime(const ScriptDesc &desc);

} // namespace sonnet::scripting
