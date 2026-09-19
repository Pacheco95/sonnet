#include <sonnet/scripting/Components.h>

#include <sonnet/world/World.h>

namespace sonnet::scripting {

void registerComponents(world::World &world) {
  if (world.findComponent("Script") != nullptr) {
    return;
  }
  world.registerComponent<Script>("Script").member<core::Uuid>("script");
}

} // namespace sonnet::scripting
