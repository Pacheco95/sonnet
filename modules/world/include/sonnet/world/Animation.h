#pragma once

#include <sonnet/world/World.h>

#include <sonnet/core/Uuid.h>

#include <flecs.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace sonnet::assets {
class AssetDatabase;
}

namespace sonnet::world {

// Skeletal animation for a world's entities (ADR-0010), constructed on a World like the
// subsystems of ADR-0009. Registers two systems:
//
// - AnimationPlayback, a simulation system in Update: every enabled Animator advances its time
//   and writes its clip's pose into the local Transforms of the entities at the clip's paths.
// - SkinPalette, in PreRender after the transform system, in edit and play mode: every enabled
//   SkinnedMesh gets the SkinPose of its joints' world transforms, which buildDrawList hands to
//   the renderer.
//
// Clips and skins are resolved through the asset database every frame; the entities they bind
// are resolved by path once and again when the asset is reloaded or a bound entity goes away.
class AnimationSystem {
public:
  AnimationSystem(World &world, assets::AssetDatabase &assets);
  ~AnimationSystem();
  AnimationSystem(const AnimationSystem &) = delete;
  AnimationSystem &operator=(const AnimationSystem &) = delete;

private:
  // The entities an asset's paths resolved to, for one entity.
  struct Binding {
    core::Uuid asset;
    std::uint64_t revision{0};
    std::vector<flecs::entity_t> targets; // per channel or joint; 0 where a path did not resolve
    std::vector<std::uint32_t> order;     // channels grouped by target, so each is written once
    std::uint64_t frame{0};               // the last frame the entity was seen, to drop the others
  };

  void play(float dt);
  void pose();
  [[nodiscard]] bool valid(const Binding &binding, const core::Uuid &asset, std::uint64_t revision) const;
  static void prune(std::unordered_map<flecs::entity_t, Binding> &bindings, std::uint64_t frame);

  World &m_world;
  assets::AssetDatabase &m_assets;
  flecs::query<> m_animators;
  flecs::query<> m_skinned;
  flecs::system m_playback;
  flecs::system m_palette;
  std::unordered_map<flecs::entity_t, Binding> m_clipBindings;
  std::unordered_map<flecs::entity_t, Binding> m_skinBindings;
  std::vector<flecs::entity> m_entities; // scratch, reused every frame
  std::uint64_t m_playFrame{0};
  std::uint64_t m_poseFrame{0};
};

} // namespace sonnet::world
