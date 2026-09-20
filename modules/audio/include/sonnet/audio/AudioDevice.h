#pragma once

#include <sonnet/audio/Components.h>

#include <sonnet/core/Math.h>
#include <sonnet/core/Uuid.h>

#include <flecs.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace sonnet::assets {
class AssetDatabase;
}

namespace sonnet::world {
class World;
}

namespace sonnet::audio {

struct AudioDesc {
  // Opens the system's default output. Without one, requested or because none could be opened,
  // the device mixes each frame's worth of audio on the main thread into lastMix instead.
  bool output{true};
  std::uint32_t sampleRate{48000};
  std::uint32_t channels{2};
};

// What a sound asset decodes to.
struct SoundInfo {
  float duration{0.0f}; // seconds
  std::uint32_t channels{0};
  std::uint32_t sampleRate{0};
};

// Sound for a world's entities (ADR-0010). A system in PreRender, after the transform system,
// plays every enabled AudioSource whose `playing` is set while the world is in play mode, placed
// by its entity's world transform relative to the listener, and stops them all in edit mode.
// Sounds are decoded on first use and again when their file changes, which restarts the sources
// playing them. The mixing, resampling and spatialisation are miniaudio's, on its own audio
// thread when there is an output device.
class IAudioDevice {
public:
  virtual ~IAudioDevice() = default;

  [[nodiscard]] virtual bool hasOutput() const = 0;
  // Where the scene is heard from when no entity has an AudioListener: the editor's camera.
  virtual void setFallbackListener(glm::vec3 position, glm::quat rotation) = 0;
  // Scales everything played; 1 is unchanged.
  virtual void setVolume(float volume) = 0;

  // Plays a sound once, unspatialised, in edit mode too: the editor's preview. Replaces the
  // previous preview; `stopPreview` ends it early.
  virtual void preview(const core::Uuid &sound) = 0;
  virtual void stopPreview() = 0;
  // Stops every sound, the preview included; the sources still marked playing start again on the
  // next frame in play mode.
  virtual void stopAll() = 0;

  // The sound's format, decoding it if needed; nothing when it is missing or not decodable.
  [[nodiscard]] virtual std::optional<SoundInfo> soundInfo(const core::Uuid &sound) = 0;
  // Sounds playing, the preview included.
  [[nodiscard]] virtual std::uint32_t playingCount() const = 0;
  [[nodiscard]] virtual bool isPlaying(flecs::entity entity) const = 0;
  // Without an output device: the interleaved frames mixed by the last frame, as many as its time
  // at the sample rate. Empty with an output device, whose mix never reaches the main thread.
  [[nodiscard]] virtual std::span<const float> lastMix() const = 0;
};

// Registers the components and the audio system on `world`, which has to outlive the result;
// sounds are resolved through `assets`. The miniaudio implementation. A missing output device is
// logged and the device runs without one.
[[nodiscard]] std::unique_ptr<IAudioDevice> createAudioDevice(world::World &world, assets::AssetDatabase &assets,
                                                              const AudioDesc &desc = {});

} // namespace sonnet::audio
