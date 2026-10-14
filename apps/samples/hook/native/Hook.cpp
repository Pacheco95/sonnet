// The hook sample's game module (ADR-0027, docs/player.md#game-modules): a component, the system
// that moves its entities, and one native Lua function. The engine calls registerGame once, in
// the player and in the editor, before any scene loads.

#include <sonnet/runtime/GameModule.h>
#include <sonnet/world/Components.h>
#include <sonnet/world/World.h>

#include <sol/sol.hpp>

#include <cmath>
#include <numbers>

namespace {

using namespace sonnet;

// Circles its entity about its parent's origin, in the XZ plane, keeping its height. Registered
// with reflection, so scenes, the inspector, undo, prefabs and Lua handle it with no code here.
struct Orbit {
  float radius{2.0f};
  float speed{1.0f}; // radians per second
};

} // namespace

void registerGame(runtime::GameRegistry &registry) {
  world::World &world = registry.world();
  world.registerComponent<Orbit>("Orbit").member<float>("radius").member<float>("speed");

  // Play mode only, as the engine's own SpinSystem. The angle comes from where the entity is, so
  // the component needs no state of its own and a scene saved mid-orbit resumes from there.
  const flecs::entity system =
      world.ecs()
          .system<world::Transform, const Orbit>("OrbitSystem")
          .kind(world.phase(world::Phase::Update))
          .without<world::Disabled>()
          .self()
          .up(flecs::ChildOf)
          .each([](flecs::iter &it, std::size_t, world::Transform &transform, const Orbit &orbit) {
            const float angle = std::atan2(transform.position.z, transform.position.x) + orbit.speed * it.delta_time();
            transform.position.x = orbit.radius * std::cos(angle);
            transform.position.z = orbit.radius * std::sin(angle);
          });
  world.addToSimulation(system);

  // What reflection does not cover: a function of the component's values, for the label's script.
  registry.bindLua([](sol::state_view lua) {
    lua["hook"] = lua.create_table_with("period", [](float speed) {
      return speed == 0.0f ? 0.0f : 2.0f * std::numbers::pi_v<float> / std::abs(speed);
    });
  });
}
