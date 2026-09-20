#pragma once

#include <sonnet/core/Uuid.h>

namespace sonnet::world {
class World;
}

namespace sonnet::audio {

// The audio components (docs/audio.md): plain structs registered with reflection in the world,
// so the inspector, scene files and scripts reach them like the core components (ADR-0010).

// Plays a sound asset from its entity in play mode while `playing` is set; a sound that does not
// loop clears it when it ends, and setting it again plays the sound from the start. Spatial
// sources are panned and attenuated from the listener: full volume within minDistance, falling
// off inversely with distance, and no quieter beyond maxDistance.
struct AudioSource {
  core::Uuid sound{};
  float volume{1.0f};
  float pitch{1.0f}; // a playback rate: 2 is an octave up and twice as fast
  bool playing{true};
  bool loop{false};
  bool spatial{true};
  float minDistance{1.0f};  // metres
  float maxDistance{50.0f}; // metres
};

// Hears the scene from its entity, facing its -Z; the first enabled one wins. Without one, the
// application's fallback listener hears it (IAudioDevice::setFallbackListener).
struct AudioListener {};

// Registers the components above with the world's reflection under their scene-file names. Done
// by createAudioDevice; separate for tools that read scenes without playing them. Idempotent.
void registerComponents(world::World &world);

} // namespace sonnet::audio
