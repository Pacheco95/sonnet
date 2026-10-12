#pragma once

#include <sonnet/core/Error.h>

#include <sol/forward.hpp>

#include <functional>
#include <string_view>

namespace sonnet::scripting {
class IScriptRuntime;
}
namespace sonnet::world {
class World;
}

namespace sonnet::runtime {

// What a game module may touch (ADR-0027), made by whoever owns the world, `Game` or the editor,
// after the engine's systems and components are registered and before any scene loads, so a
// scene that names a module's component reads it.
class GameRegistry {
public:
  GameRegistry(world::World &world, scripting::IScriptRuntime &scripts) noexcept : m_world(world), m_scripts(scripts) {
  }

  // For World::registerComponent, after which the component reaches the inspector, scenes,
  // prefabs and Lua through reflection, and for flecs systems in the world's phases, play-only
  // through World::addToSimulation.
  [[nodiscard]] world::World &world() noexcept {
    return m_world;
  }
  // Calls `bind` on the Lua state scripts run in, before any script runs, for native functions
  // and types that reflection does not cover. Components need none of it.
  void bindLua(const std::function<void(sol::state_view)> &bind);

private:
  world::World &m_world;
  scripting::IScriptRuntime &m_scripts;
};

// The game module a player or an editor was linked with: its name, as the project's `module`
// field gives it, and its entry point. The application's generated header supplies it
// (docs/player.md, "Game modules").
struct GameModule {
  std::string_view name;
  void (*registerGame)(GameRegistry &registry){nullptr};
};

// Whether a binary linked with `linked` (null for none) may open what `owner` names
// (`project "Hook"`, `bundle "Hook"`), which needs the module `needed` (empty for none). One
// that needs no module opens anywhere; one that needs another, or a module this binary lacks, is
// refused before anything is read, so a generic editor never drops a module's components on save.
// `binary` is "player" or "editor", for the message.
[[nodiscard]] core::Result<void> checkGameModule(std::string_view owner, std::string_view needed,
                                                 const GameModule *linked, std::string_view binary);

} // namespace sonnet::runtime
