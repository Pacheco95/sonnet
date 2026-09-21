#include <sonnet/audio/AudioDevice.h>
#include <sonnet/core/JobSystem.h>

#include <sonnet/assets/AssetDatabase.h>
#include <sonnet/core/File.h>
#include <sonnet/platform/Platform.h>
#include <sonnet/renderer/Renderer.h>
#include <sonnet/rhi/NullDevice.h>
#include <sonnet/world/Scene.h>
#include <sonnet/world/World.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>

using namespace sonnet;

namespace {

// A 16-bit mono WAV file of a sine at `frequency`, `seconds` long at 48 kHz.
std::vector<std::byte> sineWav(float frequency, float seconds, float amplitude = 0.5f) {
  constexpr std::uint32_t rate = 48000;
  const auto frames = static_cast<std::uint32_t>(seconds * rate);
  std::vector<std::byte> out;
  const auto put = [&](const void *data, std::size_t bytes) {
    const auto *begin = static_cast<const std::byte *>(data);
    out.insert(out.end(), begin, begin + bytes);
  };
  const auto u32 = [&](std::uint32_t value) { put(&value, 4); };
  const auto u16 = [&](std::uint16_t value) { put(&value, 2); };
  put("RIFF", 4);
  u32(36 + frames * 2);
  put("WAVEfmt ", 8);
  u32(16);
  u16(1); // PCM
  u16(1); // mono
  u32(rate);
  u32(rate * 2);
  u16(2);
  u16(16);
  put("data", 4);
  u32(frames * 2);
  for (std::uint32_t i = 0; i < frames; ++i) {
    const float value = amplitude * std::sin(2.0f * 3.14159265f * frequency * static_cast<float>(i) / rate);
    u16(static_cast<std::uint16_t>(static_cast<std::int16_t>(value * 32767.0f)));
  }
  return out;
}

// The root mean square of one channel of interleaved stereo.
float rms(std::span<const float> samples, std::size_t channel) {
  double sum = 0.0;
  std::size_t count = 0;
  for (std::size_t i = channel; i < samples.size(); i += 2) {
    sum += static_cast<double>(samples[i]) * static_cast<double>(samples[i]);
    ++count;
  }
  return count > 0 ? static_cast<float>(std::sqrt(sum / static_cast<double>(count))) : 0.0f;
}

float loudness(std::span<const float> samples) {
  return rms(samples, 0) + rms(samples, 1);
}

struct Fixture {
  platform::Platform platform{{.headless = true}};
  std::unique_ptr<rhi::NullDevice> device = rhi::createNullDevice();
  renderer::Renderer renderer{*device, platform.basePath() / "shaders"};
  core::JobSystem jobs{{.workerCount = 2}};
  assets::AssetDatabase assets{renderer, jobs};
  world::World world;
  std::filesystem::path root = std::filesystem::temp_directory_path() / "sonnet_audio";
  std::unique_ptr<audio::IAudioDevice> audio;
  core::Uuid hum;
  core::Uuid beep;
  core::Uuid broken;

  Fixture() {
    std::filesystem::remove_all(root);
    REQUIRE(core::writeFile(root / "sounds" / "hum.wav", sineWav(220.0f, 1.0f)).has_value());
    REQUIRE(core::writeFile(root / "sounds" / "beep.wav", sineWav(880.0f, 0.25f)).has_value());
    const std::string garbage = "not a sound";
    REQUIRE(core::writeFile(root / "sounds" / "broken.ogg", std::as_bytes(std::span{garbage})).has_value());
    const std::vector<std::string> roots{"sounds"};
    assets.open(root, roots);
    hum = byName("hum");
    beep = byName("beep");
    broken = byName("broken");
    audio = audio::createAudioDevice(world, assets, {.output = false});
  }
  ~Fixture() {
    audio.reset();
    std::filesystem::remove_all(root);
  }

  core::Uuid byName(std::string_view name) const {
    for (const assets::AssetInfo *info : assets.assets(assets::AssetType::Sound)) {
      if (info->name == name) {
        return info->uuid;
      }
    }
    FAIL("no sound named " << name);
  }

  flecs::entity source(std::string_view name, const audio::AudioSource &value, glm::vec3 position = glm::vec3{0.0f}) {
    const flecs::entity entity = world.createEntity(name);
    entity.set<world::Transform>({.position = position});
    entity.set<audio::AudioSource>(value);
    return entity;
  }
};

} // namespace

TEST_CASE("sources play in play mode only and fall silent when it stops", "[audio]") {
  Fixture fixture;
  REQUIRE_FALSE(fixture.audio->hasOutput());
  const flecs::entity hum =
      fixture.source("Hum", {.sound = fixture.hum, .volume = 1.0f, .playing = true, .loop = true, .spatial = false});

  // Edit mode: each frame's mix is there, and silent.
  fixture.world.progress(0.1f);
  REQUIRE(fixture.audio->lastMix().size() == std::size_t{4800} * 2);
  REQUIRE(loudness(fixture.audio->lastMix()) == 0.0f);
  REQUIRE(fixture.audio->playingCount() == 0);

  fixture.world.setPlaying(true);
  fixture.world.progress(0.1f);
  REQUIRE(fixture.audio->isPlaying(hum));
  REQUIRE(fixture.audio->playingCount() == 1);
  REQUIRE(loudness(fixture.audio->lastMix()) > 0.2f);
  // Looping outlasts the second the file holds.
  for (int i = 0; i < 15; ++i) {
    fixture.world.progress(0.1f);
  }
  REQUIRE(fixture.audio->isPlaying(hum));
  REQUIRE(loudness(fixture.audio->lastMix()) > 0.2f);

  // Clearing `playing`, disabling the entity or leaving play mode each silence it.
  hum.set<audio::AudioSource>({.sound = fixture.hum, .playing = false, .loop = true, .spatial = false});
  fixture.world.progress(0.1f);
  REQUIRE_FALSE(fixture.audio->isPlaying(hum));
  hum.set<audio::AudioSource>({.sound = fixture.hum, .playing = true, .loop = true, .spatial = false});
  fixture.world.progress(0.1f);
  REQUIRE(fixture.audio->isPlaying(hum));
  hum.add<world::Disabled>();
  fixture.world.progress(0.1f);
  REQUIRE(fixture.audio->playingCount() == 0);
  hum.remove<world::Disabled>();
  fixture.world.progress(0.1f);
  REQUIRE(fixture.audio->isPlaying(hum));
  fixture.world.setPlaying(false);
  fixture.world.progress(0.1f);
  REQUIRE(fixture.audio->playingCount() == 0);
  fixture.world.progress(0.1f);
  REQUIRE(loudness(fixture.audio->lastMix()) == 0.0f);
}

TEST_CASE("a sound that does not loop ends, clears playing, and plays again when set", "[audio]") {
  Fixture fixture;
  const flecs::entity beep = fixture.source("Beep", {.sound = fixture.beep, .spatial = false});
  fixture.world.setPlaying(true);
  fixture.world.progress(0.1f);
  REQUIRE(fixture.audio->isPlaying(beep));
  for (int i = 0; i < 4; ++i) {
    fixture.world.progress(0.1f);
  }
  REQUIRE_FALSE(beep.get<audio::AudioSource>().playing);
  REQUIRE_FALSE(fixture.audio->isPlaying(beep));
  fixture.world.progress(0.1f);
  REQUIRE(loudness(fixture.audio->lastMix()) == 0.0f);

  audio::AudioSource again = beep.get<audio::AudioSource>();
  again.playing = true;
  beep.set<audio::AudioSource>(again);
  fixture.world.progress(0.1f);
  REQUIRE(fixture.audio->isPlaying(beep));
  REQUIRE(loudness(fixture.audio->lastMix()) > 0.2f);
}

TEST_CASE("spatial sources are panned and attenuated from the listener", "[audio]") {
  Fixture fixture;
  // The listener at the origin facing -Z: +X is to its right.
  const flecs::entity listener = fixture.world.createEntity("Ears");
  listener.add<audio::AudioListener>();
  const flecs::entity right = fixture.source(
      "Right", {.sound = fixture.hum, .loop = true, .minDistance = 1.0f, .maxDistance = 100.0f}, {3.0f, 0.0f, 0.0f});
  fixture.world.setPlaying(true);
  fixture.world.progress(0.1f);
  fixture.world.progress(0.1f);
  const float nearLeft = rms(fixture.audio->lastMix(), 0);
  const float nearRight = rms(fixture.audio->lastMix(), 1);
  REQUIRE(nearRight > nearLeft * 1.5f);

  // Five times as far is quieter.
  right.set<world::Transform>({.position = {15.0f, 0.0f, 0.0f}});
  fixture.world.progress(0.1f);
  fixture.world.progress(0.1f);
  REQUIRE(loudness(fixture.audio->lastMix()) < (nearLeft + nearRight) * 0.5f);

  // Turned around, the listener hears it on the other side.
  listener.set<world::Transform>({.rotation = glm::angleAxis(glm::radians(180.0f), glm::vec3{0.0f, 1.0f, 0.0f})});
  right.set<world::Transform>({.position = {3.0f, 0.0f, 0.0f}});
  fixture.world.progress(0.1f);
  fixture.world.progress(0.1f);
  REQUIRE(rms(fixture.audio->lastMix(), 0) > rms(fixture.audio->lastMix(), 1) * 1.5f);

  // Without a listener entity the fallback hears it: placed with the source on its left.
  listener.destruct();
  fixture.audio->setFallbackListener({6.0f, 0.0f, 0.0f}, glm::quat{1.0f, 0.0f, 0.0f, 0.0f});
  fixture.world.progress(0.1f);
  fixture.world.progress(0.1f);
  REQUIRE(rms(fixture.audio->lastMix(), 0) > rms(fixture.audio->lastMix(), 1) * 1.5f);
}

TEST_CASE("sounds that cannot be decoded or found play nothing and are reported", "[audio]") {
  Fixture fixture;
  REQUIRE_FALSE(fixture.audio->soundInfo(fixture.broken).has_value());
  REQUIRE_FALSE(fixture.audio->soundInfo(core::Uuid::generate()).has_value());
  const std::optional<audio::SoundInfo> info = fixture.audio->soundInfo(fixture.beep);
  REQUIRE(info.has_value());
  REQUIRE(info->channels == 1);
  REQUIRE(info->sampleRate == 48000);
  REQUIRE(std::abs(info->duration - 0.25f) < 1e-3f);

  const flecs::entity broken = fixture.source("Broken", {.sound = fixture.broken, .loop = true});
  const flecs::entity missing = fixture.source("Missing", {.sound = core::Uuid::generate(), .loop = true});
  const flecs::entity silent = fixture.source("Silent", {.loop = true});
  fixture.world.setPlaying(true);
  fixture.world.progress(0.1f);
  fixture.world.progress(0.1f);
  REQUIRE(fixture.audio->playingCount() == 0);
  REQUIRE(broken.get<audio::AudioSource>().playing); // nothing ended: it never started
  REQUIRE(missing.get<audio::AudioSource>().playing);
  REQUIRE(silent.get<audio::AudioSource>().playing);
}

TEST_CASE("a changed sound file restarts the sources playing it", "[audio]") {
  Fixture fixture;
  const flecs::entity hum = fixture.source("Hum", {.sound = fixture.hum, .loop = true, .spatial = false});
  fixture.world.setPlaying(true);
  fixture.world.progress(0.1f);
  const float before = loudness(fixture.audio->lastMix());
  REQUIRE(before > 0.2f);

  // Quieter, and different enough for the modification time to move.
  const std::filesystem::path file = fixture.root / "sounds" / "hum.wav";
  const auto time = std::filesystem::last_write_time(file);
  for (int attempt = 0; attempt < 50 && std::filesystem::last_write_time(file) == time; ++attempt) {
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
    REQUIRE(core::writeFile(file, sineWav(220.0f, 1.0f, 0.1f)).has_value());
  }
  REQUIRE(fixture.assets.reimport(fixture.hum).has_value());
  fixture.world.progress(0.1f);
  REQUIRE(fixture.audio->isPlaying(hum));
  const float after = loudness(fixture.audio->lastMix());
  REQUIRE(after > 0.0f);
  REQUIRE(after < before * 0.5f);
}

TEST_CASE("the preview plays once in edit mode and stops on request", "[audio]") {
  Fixture fixture;
  fixture.audio->preview(fixture.beep);
  REQUIRE(fixture.audio->playingCount() == 1);
  fixture.world.progress(0.1f);
  REQUIRE(loudness(fixture.audio->lastMix()) > 0.2f);
  for (int i = 0; i < 3; ++i) {
    fixture.world.progress(0.1f);
  }
  REQUIRE(fixture.audio->playingCount() == 0);

  fixture.audio->preview(fixture.hum);
  fixture.world.progress(0.1f);
  fixture.audio->stopPreview();
  fixture.world.progress(0.1f);
  REQUIRE(loudness(fixture.audio->lastMix()) == 0.0f);
  fixture.audio->preview(fixture.broken);
  REQUIRE(fixture.audio->playingCount() == 0);
}

TEST_CASE("the audio components are registered once and round-trip through scenes", "[audio]") {
  Fixture fixture;
  audio::registerComponents(fixture.world);
  REQUIRE(fixture.world.findComponent("AudioSource") != nullptr);
  REQUIRE(fixture.world.findComponent("AudioListener")->tag);
  const flecs::entity source =
      fixture.source("Source", {.sound = fixture.beep, .volume = 0.5f, .pitch = 2.0f, .playing = false, .loop = true});
  source.add<audio::AudioListener>();
  const nlohmann::json scene = world::saveScene(fixture.world);
  fixture.world.clearScene();
  REQUIRE(world::loadScene(fixture.world, scene).has_value());
  const flecs::entity loaded = fixture.world.roots().front();
  REQUIRE(loaded.has<audio::AudioListener>());
  const audio::AudioSource &value = loaded.get<audio::AudioSource>();
  REQUIRE(value.sound == fixture.beep);
  REQUIRE(value.volume == 0.5f);
  REQUIRE(value.pitch == 2.0f);
  REQUIRE_FALSE(value.playing);
  REQUIRE(value.loop);
}
