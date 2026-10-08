#pragma once

#include <sonnet/assets/Asset.h>
#include <sonnet/core/Math.h>
#include <sonnet/core/Uuid.h>

#include <cstdint>
#include <string>
#include <vector>

namespace sonnet::world {

// The core components (docs/architecture.md, "Entity model"). Plain structs registered with
// flecs reflection in World, which drives the inspector and the scene serializer.

// The stable identity of a scene entity: what scenes, prefab references and undo refer to. Never
// inherited from a prefab and never shared.
struct Identity {
  core::Uuid uuid;
};

// Position among the siblings, from a counter in World: creation order, so a scene loaded in
// file order lists in file order however flecs recycles entity ids. Not serialized.
struct SiblingOrder {
  std::uint64_t value = 0;
};

struct Name {
  std::string value;
};

// Local transform. World matrices are derived by the transform system, never edited directly
// (docs/conventions.md, "Math conventions").
struct Transform {
  glm::vec3 position{0.0f, 0.0f, 0.0f};
  glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
  glm::vec3 scale{1.0f, 1.0f, 1.0f};

  [[nodiscard]] glm::mat4 matrix() const;
  // Decomposes an affine matrix; shear is discarded.
  [[nodiscard]] static Transform fromMatrix(const glm::mat4 &matrix);
};

// parent * local, computed by the transform system each frame. Not serialized.
struct WorldTransform {
  glm::mat4 matrix{1.0f};
};

// The `WorldTransform` of the frame before, kept for the renderer's motion vectors (ADR-0024). An
// entity gets one in the frame its world transform first exists, equal to the current matrix, so
// it has no motion until it moves. Not serialized, not inherited.
struct PreviousWorldTransform {
  glm::mat4 matrix{1.0f};
};

// A mesh by asset identity (docs/assets.md, "Identity"): a built-in primitive or a glTF mesh.
// `material` overrides every slot of the mesh; nil keeps the mesh's own materials.
struct MeshRenderer {
  core::Uuid mesh{assets::builtin::box()};
  core::Uuid material{};
  glm::vec4 color{0.8f, 0.8f, 0.8f, 1.0f}; // multiplies the material's base colour
  bool visible{true};
};

// Looks down the entity's -Z. The first camera in the scene is the one the player renders from.
struct Camera {
  float fovY{glm::radians(60.0f)}; // radians; the inspector shows degrees
  float nearPlane{0.1f};
};

// Shines along the entity's -Z.
struct DirectionalLight {
  glm::vec3 color{1.0f, 0.96f, 0.9f};
  float intensity{3.0f};
};

// Punctual lights at their entity's position; intensity is radiance at one metre.
struct PointLight {
  glm::vec3 color{1.0f, 1.0f, 1.0f};
  float intensity{10.0f};
  float range{10.0f};
  bool castsShadows{false}; // asks for a shadow cube map; scenes without it stay unshadowed
};

// Shines along the entity's -Z.
struct SpotLight {
  glm::vec3 color{1.0f, 1.0f, 1.0f};
  float intensity{10.0f};
  float range{10.0f};
  float innerAngle{glm::radians(20.0f)};
  float outerAngle{glm::radians(30.0f)};
  bool castsShadows{false}; // asks for a shadow map; scenes without it stay unshadowed
};

// The scene's image-based lighting and skybox, by environment asset; the first entity that has
// one wins. Without one the renderer lights with its flat ambient term.
struct Environment {
  core::Uuid map{};
  float intensity{1.0f};
  float exposure{1.0f};
};

// Deforms the entity's MeshRenderer mesh by a skin asset's joints (ADR-0010): the joints are the
// entities at the skin's paths under the nearest ancestor where all of them resolve, which for a
// model instance is its root. Without AnimationSystem, or while the joints cannot be found, the
// mesh draws in its bind pose.
struct SkinnedMesh {
  core::Uuid skin{};
};

// A clip playing besides an Animator's own, blended with it by weight (ADR-0023). A layer whose
// `fadeRate` is positive gains weight to 1 and stops there; a negative one loses it and is
// removed at zero, which is how a crossfade lets the clip it left go.
struct AnimationLayer {
  core::Uuid clip{};
  float time{0.0f}; // seconds
  float speed{1.0f};
  float weight{1.0f};
  float fadeRate{0.0f}; // weight per second
  bool playing{true};
  bool loop{true};

  bool operator==(const AnimationLayer &) const = default;
};

// Plays an animation clip on the entity's hierarchy in play mode (ADR-0010): each channel drives
// the local Transform of the entity at its path under this one. `time` advances by `speed` while
// `playing`; a clip that does not loop stops at its end and clears `playing`. The pose at `time`
// is written every frame there is a clip, so setting `time` while stopped scrubs.
//
// ADR-0023: assigning `clip` while `fade` is above zero is a crossfade, over `fade` seconds, from
// the clip it replaces. `layers` play together with it, the targets' poses being the weighted
// blend of every layer that has a channel for them. With `rootMotion`, the translation of the
// bone at `rootBone` (empty: the clip's translation channel with the shortest path) is taken
// out of the pose and moves this entity instead.
struct Animator {
  core::Uuid clip{};
  float time{0.0f}; // seconds
  float speed{1.0f};
  bool playing{true};
  bool loop{true};
  float fade{0.0f}; // seconds
  bool rootMotion{false};
  std::string rootBone{};
  std::vector<AnimationLayer> layers{};
};

// The weights of the morph targets of the entity's MeshRenderer mesh, in the mesh's order
// (ADR-0023): the mesh draws deformed by each target's deltas times its weight. Animated by clips
// with a Weights channel; a model's prefab starts them at the glTF file's weights.
struct MorphWeights {
  std::vector<float> weights{};

  bool operator==(const MorphWeights &) const = default;
};

// 32-bit, the width reflection reads enum values with.
enum class ParticleBlendMode : std::int32_t {
  Alpha,
  Additive, // glows: colours add up instead of covering what is behind
};

enum class ParticleSpace : std::int32_t {
  World, // particles stay where they were born when the emitter moves
  Local, // they move with it
};

// Emits particles simulated on the GPU (ADR-0023): `rate` a second, and `burst` more when it
// first appears, each flying off within `coneAngle` of the entity's +Y at a speed between
// `speedMin` and `speedMax` for a lifetime between `lifetimeMin` and `lifetimeMax`, pulled by
// `gravity` and slowed by `drag`, growing from `sizeStart` to `sizeEnd` metres across and
// shifting from `colorStart` to `colorEnd` (linear, so a value above 1 glows). At most
// `maxParticles` live at once; a new one takes the oldest's place. The same `seed` makes the
// same particles. It plays in play mode, and in the editor while the entity is selected.
struct ParticleEmitter {
  bool playing{true};
  std::uint32_t maxParticles{1000};
  float rate{50.0f};
  std::uint32_t burst{0};
  float lifetimeMin{1.0f};
  float lifetimeMax{2.0f};
  float speedMin{1.0f};
  float speedMax{2.0f};
  float coneAngle{0.5235988f}; // radians, 30 degrees
  glm::vec3 gravity{0.0f, -9.8f, 0.0f};
  float drag{0.0f};
  float sizeStart{0.1f};
  float sizeEnd{0.1f};
  glm::vec4 colorStart{1.0f, 1.0f, 1.0f, 1.0f};
  glm::vec4 colorEnd{1.0f, 1.0f, 1.0f, 0.0f};
  core::Uuid texture{}; // a texture asset, or none for a soft disc
  ParticleBlendMode blend{ParticleBlendMode::Alpha};
  ParticleSpace space{ParticleSpace::World};
  std::uint32_t seed{1};
};

// The joint matrices of a SkinnedMesh for the current pose, each from the mesh's bind pose into
// its entity's space; computed every frame by AnimationSystem, never saved or inherited.
struct SkinPose {
  std::vector<glm::mat4> joints;
};

// Turns the entity about `axis` in play mode: the script-free behaviour of M2.
struct Spin {
  glm::vec3 axis{0.0f, 1.0f, 0.0f};
  float speed{1.0f}; // radians per second
};

struct Static {};
struct EditorOnly {}; // never serialized into scenes
struct Disabled {};   // skipped by the draw list and the simulation systems

} // namespace sonnet::world
