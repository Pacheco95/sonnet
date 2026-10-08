#include <sonnet/scripting/Components.h>

#include <sonnet/world/World.h>

namespace sonnet::scripting {

void registerComponents(world::World &world) {
  if (world.findComponent("Scripts") != nullptr) {
    return;
  }
  world.ecs().component<ScriptSlot>("ScriptSlot").member<core::Uuid>("script").member<std::string>("properties");
  world.registerVector<ScriptSlot>();
  world.registerComponent<Scripts>("Scripts").member<std::vector<ScriptSlot>>("slots");
  world.embedJson("Scripts", "properties");
}

} // namespace sonnet::scripting
