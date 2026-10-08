#pragma once

#include <sonnet/core/Uuid.h>

#include <string>
#include <vector>

namespace sonnet::world {
class World;
}

namespace sonnet::scripting {

// One script on an entity: the asset and the property values its author changed, as the text of
// a JSON object keyed by property name (scene files show it as the object itself). Empty means
// every property has the class's default.
struct ScriptSlot {
  core::Uuid script{};
  std::string properties{};
};

// The scripts an entity runs in play mode, in slot order (ADR-0022, docs/scripting.md).
struct Scripts {
  std::vector<ScriptSlot> slots{};
};

// Registers Scripts with the world's reflection under its scene-file name. Done by
// createScriptRuntime; separate for tools that read scenes without running them. Idempotent.
void registerComponents(world::World &world);

} // namespace sonnet::scripting
