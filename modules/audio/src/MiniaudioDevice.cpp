#include <sonnet/audio/AudioDevice.h>

#include <sonnet/assets/AssetDatabase.h>
#include <sonnet/core/Log.h>
#include <sonnet/core/Profile.h>
#include <sonnet/world/Components.h>
#include <sonnet/world/World.h>

#include <miniaudio.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace sonnet::audio {

namespace {

std::string describe(flecs::entity entity) {
  const world::Name *name = entity.try_get<world::Name>();
  return name != nullptr ? name->value : std::format("entity {}", entity.id());
}

glm::mat4 worldMatrixOf(flecs::entity entity) {
  const world::WorldTransform *transform = entity.try_get<world::WorldTransform>();
  return transform != nullptr ? transform->matrix : world::World::worldMatrix(entity);
}

class MiniaudioDevice final : public IAudioDevice {
public:
  MiniaudioDevice(world::World &world, assets::AssetDatabase &assets, const AudioDesc &desc)
      : m_world(world), m_assets(assets) {
    registerComponents(world);
    ma_engine_config config = ma_engine_config_init();
    config.channels = desc.channels;
    config.sampleRate = desc.sampleRate;
    m_hasOutput = desc.output;
    if (desc.output) {
      if (const ma_result result = ma_engine_init(&config, &m_engine); result != MA_SUCCESS) {
        SONNET_LOG_WARN("no audio output device ({}), running silent", ma_result_description(result));
        m_hasOutput = false;
      }
    }
    if (!m_hasOutput) {
      config.noDevice = MA_TRUE;
      if (const ma_result result = ma_engine_init(&config, &m_engine); result != MA_SUCCESS) {
        throw core::Exception{
            core::Error{std::format("audio engine: {}", ma_result_description(result)), core::ErrorCategory::Platform}};
      }
    }
    m_sampleRate = ma_engine_get_sample_rate(&m_engine);
    m_channels = ma_engine_get_channels(&m_engine);
    ma_engine_listener_set_world_up(&m_engine, 0, 0.0f, 1.0f, 0.0f);
    setFallbackListener(glm::vec3{0.0f}, glm::quat{1.0f, 0.0f, 0.0f, 0.0f});

    flecs::world &ecs = world.ecs();
    m_sources = ecs.query_builder<>("AudioSources").with<AudioSource>().without<world::Disabled>().build();
    m_listeners = ecs.query_builder<>("AudioListeners").with<AudioListener>().without<world::Disabled>().build();
    // Not a simulation system: in edit mode it stops what play mode left playing and mixes the
    // preview. Declared after the transform system, so positions are this frame's.
    m_system = ecs.system("Audio").kind(world.phase(world::Phase::PreRender)).run([this](flecs::iter &it) {
      update(it.delta_time());
    });
    SONNET_LOG_DEBUG("audio ready: {} Hz, {} channels, {}", m_sampleRate, m_channels,
                     m_hasOutput ? "output device" : "no output device");
  }

  ~MiniaudioDevice() override {
    m_system.destruct();
    m_listeners.destruct();
    m_sources.destruct();
    stopAll();
    ma_engine_uninit(&m_engine);
  }

  MiniaudioDevice(const MiniaudioDevice &) = delete;
  MiniaudioDevice &operator=(const MiniaudioDevice &) = delete;

  bool hasOutput() const override {
    return m_hasOutput;
  }

  void setFallbackListener(glm::vec3 position, glm::quat rotation) override {
    m_fallbackPosition = position;
    m_fallbackRotation = rotation;
  }

  void setVolume(float volume) override {
    ma_engine_set_volume(&m_engine, std::max(volume, 0.0f));
  }

  void preview(const core::Uuid &sound) override {
    stopPreview();
    const Clip *clip = load(sound);
    if (clip == nullptr) {
      return;
    }
    m_preview = start(sound, *clip);
    if (m_preview) {
      ma_sound_set_spatialization_enabled(&m_preview->playback, MA_FALSE);
      ma_sound_start(&m_preview->playback);
    }
  }

  void stopPreview() override {
    m_preview.reset();
  }

  void stopAll() override {
    m_voices.clear();
    m_preview.reset();
  }

  std::optional<SoundInfo> soundInfo(const core::Uuid &sound) override {
    const Clip *clip = load(sound);
    if (clip == nullptr) {
      return std::nullopt;
    }
    return SoundInfo{.duration = static_cast<float>(clip->frames) / static_cast<float>(clip->sampleRate),
                     .channels = clip->channels,
                     .sampleRate = clip->sampleRate};
  }

  std::uint32_t playingCount() const override {
    const auto playing = [](const std::unique_ptr<Voice> &voice) { return voice && !voice->ended(); };
    return static_cast<std::uint32_t>(
               std::ranges::count_if(m_voices, [&](const auto &entry) { return playing(entry.second); })) +
           (playing(m_preview) ? 1u : 0u);
  }

  bool isPlaying(flecs::entity entity) const override {
    const auto it = m_voices.find(entity.id());
    return it != m_voices.end() && !it->second->ended();
  }

  std::span<const float> lastMix() const override {
    return m_mix;
  }

private:
  // A decoded sound, interleaved 32-bit float at its own rate; miniaudio resamples as it mixes.
  struct Clip {
    std::vector<float> samples;
    std::uint64_t frames{0};
    std::uint32_t channels{0};
    std::uint32_t sampleRate{0};
    std::uint64_t revision{0};
    bool failed{false}; // this revision could not be decoded; reported once
  };

  // A sound playing one clip. Neither miniaudio object may move once initialised, hence the
  // heap; the sound reads the clip's samples through the buffer reference.
  struct Voice {
    core::Uuid sound;
    std::uint64_t revision{0};
    ma_audio_buffer_ref source{};
    ma_sound playback{};
    bool initialised{false};

    Voice() = default;
    Voice(const Voice &) = delete;
    Voice &operator=(const Voice &) = delete;
    ~Voice() {
      if (initialised) {
        ma_sound_uninit(&playback);
      }
      ma_audio_buffer_ref_uninit(&source);
    }
    [[nodiscard]] bool ended() const {
      return !ma_sound_is_looping(&playback) && ma_sound_at_end(&playback);
    }
  };

  // The sound's clip, decoded on first use and again for a new revision of the file, which first
  // ends every voice playing the old one; null when the sound is missing or not decodable.
  const Clip *load(const core::Uuid &uuid) {
    const assets::SoundSource *source = m_assets.sound(uuid);
    if (source == nullptr) {
      return nullptr;
    }
    Clip &clip = m_clips[uuid];
    if (clip.revision == source->revision) {
      return clip.failed ? nullptr : &clip;
    }
    std::erase_if(m_voices, [&](const auto &entry) { return entry.second->sound == uuid; });
    if (m_preview && m_preview->sound == uuid) {
      m_preview.reset();
    }
    clip =
        Clip{.samples = {}, .frames = 0, .channels = 0, .sampleRate = 0, .revision = source->revision, .failed = true};
    const assets::AssetInfo *info = m_assets.find(uuid);
    const std::string name = info != nullptr ? info->source.filename().string() : uuid.toString();
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
    ma_decoder decoder;
    if (ma_decoder_init_memory(source->bytes.data(), source->bytes.size(), &config, &decoder) != MA_SUCCESS) {
      SONNET_LOG_ERROR("{}: not a WAV, FLAC, MP3 or Ogg Vorbis file", name);
      return nullptr;
    }
    clip.channels = decoder.outputChannels;
    clip.sampleRate = decoder.outputSampleRate;
    // Read to the end in blocks: compressed formats do not always know their length up front.
    std::array<float, 4096> block{};
    const ma_uint64 blockFrames = block.size() / std::max<std::uint32_t>(clip.channels, 1);
    for (;;) {
      ma_uint64 read = 0;
      const ma_result result = ma_decoder_read_pcm_frames(&decoder, block.data(), blockFrames, &read);
      clip.samples.insert(clip.samples.end(), block.begin(),
                          block.begin() + static_cast<std::ptrdiff_t>(read * clip.channels));
      if (result != MA_SUCCESS || read < blockFrames) {
        break;
      }
    }
    ma_decoder_uninit(&decoder);
    clip.frames = clip.channels > 0 ? clip.samples.size() / clip.channels : 0;
    if (clip.frames == 0 || clip.sampleRate == 0) {
      SONNET_LOG_ERROR("{}: no audio in the file", name);
      return nullptr;
    }
    clip.failed = false;
    SONNET_LOG_DEBUG("{}: {} frames, {} channels at {} Hz", name, clip.frames, clip.channels, clip.sampleRate);
    return &clip;
  }

  // A voice for the clip, not yet started; null when miniaudio refuses it.
  std::unique_ptr<Voice> start(const core::Uuid &uuid, const Clip &clip) {
    auto voice = std::make_unique<Voice>();
    voice->sound = uuid;
    voice->revision = clip.revision;
    if (ma_audio_buffer_ref_init(ma_format_f32, clip.channels, clip.samples.data(), clip.frames, &voice->source) !=
        MA_SUCCESS) {
      return nullptr;
    }
    // miniaudio 0.11 leaves the reference's rate at 0, which it reads as the engine's own.
    voice->source.sampleRate = clip.sampleRate;
    if (const ma_result result =
            ma_sound_init_from_data_source(&m_engine, &voice->source, 0, nullptr, &voice->playback);
        result != MA_SUCCESS) {
      SONNET_LOG_ERROR("audio: a voice could not be created: {}", ma_result_description(result));
      return nullptr;
    }
    voice->initialised = true;
    return voice;
  }

  void placeListener() {
    glm::vec3 position = m_fallbackPosition;
    glm::quat rotation = m_fallbackRotation;
    bool found = false;
    m_listeners.each([&](flecs::entity entity) {
      if (found) {
        return;
      }
      found = true;
      const world::Transform placed = world::Transform::fromMatrix(worldMatrixOf(entity));
      position = placed.position;
      rotation = placed.rotation;
    });
    const glm::vec3 forward = rotation * glm::vec3{0.0f, 0.0f, -1.0f};
    const glm::vec3 up = rotation * glm::vec3{0.0f, 1.0f, 0.0f};
    ma_engine_listener_set_position(&m_engine, 0, position.x, position.y, position.z);
    ma_engine_listener_set_direction(&m_engine, 0, forward.x, forward.y, forward.z);
    ma_engine_listener_set_world_up(&m_engine, 0, up.x, up.y, up.z);
  }

  void update(float dt) {
    SONNET_ZONE();
    if (m_preview && m_preview->ended()) {
      m_preview.reset();
    }
    if (!m_world.isPlaying()) {
      m_voices.clear();
    } else {
      placeListener();
      m_entities.clear();
      m_sources.each([&](flecs::entity entity) { m_entities.push_back(entity); });
      std::erase_if(m_voices, [&](const auto &entry) {
        return std::ranges::none_of(m_entities, [&](flecs::entity entity) { return entity.id() == entry.first; });
      });
      for (const flecs::entity entity : m_entities) {
        play(entity);
      }
    }
    if (!m_hasOutput) {
      mix(dt);
    }
  }

  // Starts, updates or ends the entity's voice to match its AudioSource.
  void play(flecs::entity entity) {
    const AudioSource source = *entity.try_get<AudioSource>();
    if (!source.playing) {
      m_voices.erase(entity.id());
      return;
    }
    const Clip *clip = load(source.sound); // may end voices of an older revision
    auto it = m_voices.find(entity.id());
    if (it != m_voices.end() && (clip == nullptr || it->second->sound != source.sound)) {
      m_voices.erase(it);
      it = m_voices.end();
    }
    if (clip == nullptr) {
      if (!source.sound.isNil() && m_assets.find(source.sound) == nullptr && m_reported.insert(entity.id()).second) {
        SONNET_LOG_WARN("{}: sound {} is not in the project", describe(entity), source.sound.toString());
      }
      return;
    }
    if (it != m_voices.end() && it->second->ended()) {
      // A finished sound clears `playing`, so setting it again plays it from the start.
      m_voices.erase(it);
      AudioSource finished = source;
      finished.playing = false;
      entity.set<AudioSource>(finished);
      return;
    }
    Voice *voice = nullptr;
    bool fresh = false;
    if (it != m_voices.end()) {
      voice = it->second.get();
    } else if (std::unique_ptr<Voice> started = start(source.sound, *clip)) {
      voice = started.get();
      m_voices.emplace(entity.id(), std::move(started));
      fresh = true;
    } else {
      return;
    }
    ma_sound &sound = voice->playback;
    ma_sound_set_volume(&sound, std::max(source.volume, 0.0f));
    ma_sound_set_pitch(&sound, std::max(source.pitch, 0.01f));
    ma_sound_set_looping(&sound, source.loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_spatialization_enabled(&sound, source.spatial ? MA_TRUE : MA_FALSE);
    if (source.spatial) {
      const glm::vec3 position{worldMatrixOf(entity)[3]};
      ma_sound_set_position(&sound, position.x, position.y, position.z);
      ma_sound_set_attenuation_model(&sound, ma_attenuation_model_inverse);
      ma_sound_set_min_distance(&sound, std::max(source.minDistance, 0.01f));
      ma_sound_set_max_distance(&sound, std::max(source.maxDistance, source.minDistance));
    }
    if (fresh) {
      ma_sound_start(&sound);
    }
  }

  // Without a device, what the device's thread would have pulled for this frame.
  void mix(float dt) {
    m_mixCarry += static_cast<double>(dt) * m_sampleRate;
    const auto frames = static_cast<ma_uint64>(m_mixCarry);
    m_mixCarry -= static_cast<double>(frames);
    m_mix.assign(static_cast<std::size_t>(frames) * m_channels, 0.0f);
    if (frames > 0) {
      ma_engine_read_pcm_frames(&m_engine, m_mix.data(), frames, nullptr);
    }
  }

  world::World &m_world;
  assets::AssetDatabase &m_assets;
  // Declared before the voices, which read the clips' samples and go first.
  std::unordered_map<core::Uuid, Clip> m_clips;
  ma_engine m_engine{};
  bool m_hasOutput{false};
  std::uint32_t m_sampleRate{0};
  std::uint32_t m_channels{0};
  std::unordered_map<flecs::entity_t, std::unique_ptr<Voice>> m_voices;
  std::unique_ptr<Voice> m_preview;
  std::unordered_set<flecs::entity_t> m_reported;
  glm::vec3 m_fallbackPosition{0.0f};
  glm::quat m_fallbackRotation{1.0f, 0.0f, 0.0f, 0.0f};
  flecs::query<> m_sources;
  flecs::query<> m_listeners;
  flecs::system m_system;
  std::vector<flecs::entity> m_entities;
  std::vector<float> m_mix;
  double m_mixCarry{0.0};
};

} // namespace

std::unique_ptr<IAudioDevice> createAudioDevice(world::World &world, assets::AssetDatabase &assets,
                                                const AudioDesc &desc) {
  return std::make_unique<MiniaudioDevice>(world, assets, desc);
}

} // namespace sonnet::audio
