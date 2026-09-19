#pragma once

#include <sonnet/core/Uuid.h>

namespace sonnet::world {
class World;
}

namespace sonnet::scripting {

// Runs a Lua script asset on its entity in play mode (docs/scripting.md). One per entity.
struct Script {
  core::Uuid script{};
};

// Registers Script with the world's reflection under its scene-file name. Done by
// createScriptRuntime; separate for tools that read scenes without running them. Idempotent.
void registerComponents(world::World &world);

} // namespace sonnet::scripting
