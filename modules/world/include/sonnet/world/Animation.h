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
struct LegIk;

// A named moment of a clip that an Animator crossed in the last Update (ADR-0023); `scripting`
// delivers them to the entity's scripts.
struct AnimationEventRecord {
  flecs::entity_t entity{0};
  float time{0.0f};
  std::string name;
  std::string argument;
};

// Skeletal animation for a world's entities (ADR-0010), constructed on a World like the
// subsystems of ADR-0009. Registers three systems:
//
// - AnimationPlayback, a simulation system in Update: every enabled Animator advances its time
//   and writes its clip's pose into the local Transforms of the entities at the clip's paths.
// - LegIk, in PreRender after the transform system and before SkinPalette (ADR-0042): every
//   enabled LegIk rewrites the world matrices, never the local Transforms, of its pelvis subtree
//   so the feet meet the ground the game wrote into the component.
// - SkinPalette, in PreRender after LegIk, in edit and play mode: every enabled SkinnedMesh gets
//   the SkinPose of its joints' world transforms, which buildDrawList hands to the renderer.
//
// ADR-0023: playback blends the Animator's clip with its layers by weight, crossfades when the
// clip is assigned with a fade, takes root motion out of the pose and records the animation
// events it crossed, which events() returns until the next Update. ADR-0038: the clip's weight
// is the Animator's `weight` times the crossfade's ramp, and a layer with `sync` follows the
// clip's phase. The clips a crossfade leaves fade out in the system's own state, beside the
// Animator's `layers` rather than in them, so a script can replace the list every frame.
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

  // A clip binding is per entity and layer: slot 0 is the Animator's own clip, 1 up its layers,
  // FadingSlot up the clips its crossfades left.
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

  static constexpr std::uint32_t FadingSlot = 1u << 16;

  // What an Animator did last frame, to see a clip replaced and fade out of the old one.
  struct PlayState {
    core::Uuid clip;
    float time{0.0f};
    float speed{1.0f};
    bool loop{true};
    float ramp{1.0f};      // the crossfade's envelope on the Animator's own clip
    float effective{1.0f}; // the clip's blend weight last frame: the Animator's weight times the ramp
    float fadeSeconds{0.0f};
    std::vector<AnimationLayer> fading{}; // the clips crossfades left, losing weight
    bool known{false};
    std::uint64_t frame{0};
  };

  // What a LegIk's paths resolved to, per leg, and what eases from frame to frame (ADR-0042).
  struct LegBinding {
    flecs::entity_t thigh{0};
    flecs::entity_t calf{0};
    flecs::entity_t foot{0};
    bool usable{false};     // all three found, one under the other, and under the pelvis
    float restHeight{0.0f}; // the foot's height in the skin's bind pose, above the model's origin
    bool restKnown{false};
    bool restMissing{false}; // no skin has the joint: the foot counts as planted
    float offset{0.0f};      // the ground's height above the model's origin, eased
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    float ground{0.0f}; // how much the ground query is trusted, eased towards 1 or 0
  };
  struct LegState {
    std::vector<std::string> paths; // pelvis, then each leg's thigh, calf and foot, as bound
    flecs::entity_t pelvis{0};
    std::vector<LegBinding> legs;
    float weight{0.0f}; // the eased overall weight
    float drop{0.0f};   // the eased pelvis drop, before the weight
    glm::vec3 origin{0.0f};
    bool known{false};
    std::uint64_t frame{0};
  };
  // One entity of a pelvis subtree: its world matrix as the transform system left it, and the
  // world-space transform that moves it.
  struct LegNode {
    flecs::entity_t entity{0};
    std::uint32_t parent{0};
    glm::mat4 matrix{1.0f};
    glm::mat4 delta{1.0f};
    bool moved{false};
  };
  // One leg's joints this frame, what it asks of the pelvis, and the world-space transforms its
  // bones move by.
  struct LegWork {
    std::size_t index{0};
    glm::vec3 hip{0.0f};
    glm::vec3 knee{0.0f};
    glm::vec3 ankle{0.0f};
    float contact{1.0f};
    glm::mat4 thigh{1.0f};
    glm::mat4 calf{1.0f};
    glm::mat4 foot{1.0f};
  };

  // Where a foot's bind-pose height stands: found, waiting for a skin to load, or in none.
  enum class Rest : std::uint8_t {
    Known,
    Waiting,
    Missing
  };

  // The Animator's phase this frame, for synchronised layers: `phased` while it has a clip of
  // some duration.
  struct PrimaryPhase {
    bool phased{false};
    float from{0.0f};
    float to{0.0f};
    float speed{0.0f};
    bool loop{true};
    bool playing{false};
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
  void legIk(float dt);
  void legIkOf(flecs::entity entity, const LegIk &ik, float dt);
  // Resolves the paths of a LegIk into `state`, again when they or a bound entity change; false
  // when the pelvis is not found.
  bool bindLegs(flecs::entity entity, const LegIk &ik, LegState &state);
  // The foot's bind-pose height, from the skin of a mesh under `root` that has the joint.
  Rest restHeight(flecs::entity root, const std::string &foot, float &height);
  void pose();
  [[nodiscard]] bool valid(const Binding &binding, const core::Uuid &asset, std::uint64_t revision) const;
  // Advances one layer, by its own time or by the phase; false when it faded out and goes.
  bool advanceLayer(AnimationLayer &layer, std::uint32_t slot, const PrimaryPhase &phase, float dt);
  const Binding &bind(flecs::entity entity, std::uint32_t slot, const core::Uuid &asset,
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
  flecs::query<> m_legged;
  flecs::system m_playback;
  flecs::system m_legIk;
  flecs::system m_palette;
  std::unordered_map<LayerKey, Binding, LayerKeyHash> m_clipBindings;
  std::unordered_map<flecs::entity_t, PlayState> m_states;
  std::vector<Active> m_active;                                   // scratch, reused every frame
  std::vector<float> m_values;                                    // scratch: one sampled value
  std::vector<TargetPose> m_poses;                                // scratch
  std::unordered_map<flecs::entity_t, std::uint32_t> m_poseIndex; // scratch: target -> m_poses
  std::vector<AnimationEventRecord> m_events;
  std::unordered_map<flecs::entity_t, Binding> m_skinBindings;
  std::unordered_map<flecs::entity_t, LegState> m_legStates;
  std::vector<LegNode> m_legNodes;       // scratch
  std::vector<LegWork> m_legWork;        // scratch
  std::vector<flecs::entity> m_entities; // scratch, reused every frame
  std::uint64_t m_playFrame{0};
  std::uint64_t m_poseFrame{0};
  std::uint64_t m_legFrame{0};
};

} // namespace sonnet::world
