#pragma once

#include <sonnet/world/World.h>

#include <sonnet/core/Uuid.h>

#include <flecs.h>

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace sonnet::assets {
class AssetDatabase;
struct AnimationClip;
} // namespace sonnet::assets

namespace sonnet::world {

struct Animator;

// A named moment of a clip that an Animator crossed in the last Update (ADR-0023); `scripting`
// delivers them to the entity's scripts.
struct AnimationEventRecord {
  flecs::entity_t entity{0};
  float time{0.0f};
  std::string name;
  std::string argument;
};

// Skeletal animation for a world's entities (ADR-0010), constructed on a World like the
// subsystems of ADR-0009. Registers two systems:
//
// - AnimationPlayback, a simulation system in Update: every enabled Animator advances its time
//   and writes its clip's pose into the local Transforms of the entities at the clip's paths.
// - SkinPalette, in PreRender after the transform system, in edit and play mode: every enabled
//   SkinnedMesh gets the SkinPose of its joints' world transforms, which buildDrawList hands to
//   the renderer.
//
// ADR-0023: playback blends the Animator's clip with its layers by weight, crossfades when the
// clip is assigned with a fade, takes root motion out of the pose and records the animation
// events it crossed, which events() returns until the next Update.
//
// Clips and skins are resolved through the asset database every frame; the entities they bind
// are resolved by path once and again when the asset is reloaded or a bound entity goes away.
class AnimationSystem {
public:
  AnimationSystem(World &world, assets::AssetDatabase &assets);
  ~AnimationSystem();
  AnimationSystem(const AnimationSystem &) = delete;
  AnimationSystem &operator=(const AnimationSystem &) = delete;

  // The events of the last Update, ordered by entity.
  [[nodiscard]] std::span<const AnimationEventRecord> events() const {
    return m_events;
  }

private:
  // The entities an asset's paths resolved to, for one entity.
  struct Binding {
    core::Uuid asset;
    std::uint64_t revision{0};
    std::vector<flecs::entity_t> targets; // per channel or joint; 0 where a path did not resolve
    std::uint64_t frame{0};               // the last frame the entity was seen, to drop the others
  };

  // A clip binding is per entity and layer: slot 0 is the Animator's own clip.
  struct LayerKey {
    flecs::entity_t entity{0};
    std::uint32_t slot{0};
    bool operator==(const LayerKey &) const = default;
  };
  struct LayerKeyHash {
    std::size_t operator()(const LayerKey &key) const noexcept {
      return std::hash<std::uint64_t>{}(key.entity) * 31u + key.slot;
    }
  };

  // What an Animator did last frame, to see a clip replaced and fade out of the old one.
  struct PlayState {
    core::Uuid clip;
    float time{0.0f};
    float speed{1.0f};
    bool loop{true};
    float weight{1.0f}; // of the Animator's own clip
    float fadeSeconds{0.0f};
    bool known{false};
    std::uint64_t frame{0};
  };

  // A clip playing this frame, moved from `from` to `to`.
  struct Active {
    const assets::AnimationClip *clip{nullptr};
    float from{0.0f};
    float to{0.0f};
    float speed{1.0f};
    float weight{1.0f};
    bool loop{true};
    bool playing{true};
    std::uint32_t slot{0};
  };

  // The blend of one target's channels so far.
  struct TargetPose {
    flecs::entity_t target{0};
    glm::vec3 position{0.0f};
    float positionWeight{0.0f};
    glm::vec3 scale{0.0f};
    float scaleWeight{0.0f};
    glm::vec4 rotation{0.0f};
    float rotationWeight{0.0f};
    std::vector<float> morph{}; // a Weights channel's blend: sum of weight * value
    float morphWeight{0.0f};
  };

  void play(float dt);
  void pose();
  [[nodiscard]] bool valid(const Binding &binding, const core::Uuid &asset, std::uint64_t revision) const;
  const Binding &bind(flecs::entity entity, std::uint32_t slot, const Animator &animator,
                      const assets::AnimationClip &clip);
  void applyRootMotion(flecs::entity entity, const Animator &animator, const assets::AnimationClip &clip, float from,
                       float weight);
  void collectEvents(flecs::entity entity, const Active &active);
  template <typename Map> static void prune(Map &bindings, std::uint64_t frame) {
    std::erase_if(bindings, [frame](const auto &entry) { return entry.second.frame != frame; });
  }

  World &m_world;
  assets::AssetDatabase &m_assets;
  flecs::query<> m_animators;
  flecs::query<> m_skinned;
  flecs::system m_playback;
  flecs::system m_palette;
  std::unordered_map<LayerKey, Binding, LayerKeyHash> m_clipBindings;
  std::unordered_map<flecs::entity_t, PlayState> m_states;
  std::vector<Active> m_active;                                   // scratch, reused every frame
  std::vector<float> m_values;                                    // scratch: one sampled value
  std::vector<TargetPose> m_poses;                                // scratch
  std::unordered_map<flecs::entity_t, std::uint32_t> m_poseIndex; // scratch: target -> m_poses
  std::vector<AnimationEventRecord> m_events;
  std::unordered_map<flecs::entity_t, Binding> m_skinBindings;
  std::vector<flecs::entity> m_entities; // scratch, reused every frame
  std::uint64_t m_playFrame{0};
  std::uint64_t m_poseFrame{0};
};

} // namespace sonnet::world
