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
};

// Shines along the entity's -Z.
struct SpotLight {
  glm::vec3 color{1.0f, 1.0f, 1.0f};
  float intensity{10.0f};
  float range{10.0f};
  float innerAngle{glm::radians(20.0f)};
  float outerAngle{glm::radians(30.0f)};
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

// Plays an animation clip on the entity's hierarchy in play mode (ADR-0010): each channel drives
// the local Transform of the entity at its path under this one. `time` advances by `speed` while
// `playing`; a clip that does not loop stops at its end and clears `playing`. The pose at `time`
// is written every frame there is a clip, so setting `time` while stopped scrubs.
struct Animator {
  core::Uuid clip{};
  float time{0.0f}; // seconds
  float speed{1.0f};
  bool playing{true};
  bool loop{true};
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
