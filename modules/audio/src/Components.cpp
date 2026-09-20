#include <sonnet/audio/Components.h>

#include <sonnet/world/World.h>

namespace sonnet::audio {

void registerComponents(world::World &world) {
  if (world.findComponent("AudioSource") != nullptr) {
    return;
  }
  world.registerComponent<AudioSource>("AudioSource")
      .member<core::Uuid>("sound")
      .member<float>("volume")
      .member<float>("pitch")
      .member<bool>("playing")
      .member<bool>("loop")
      .member<bool>("spatial")
      .member<float>("minDistance")
      .member<float>("maxDistance");
  world.registerComponent<AudioListener>("AudioListener", true);
}

} // namespace sonnet::audio
