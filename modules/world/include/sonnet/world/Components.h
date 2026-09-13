#pragma once

#include <sonnet/core/Math.h>
#include <sonnet/core/Uuid.h>

#include <cstdint>
#include <string>

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

// Until assets arrive in M3 a mesh is one of the renderer's primitives.
enum class Primitive : std::int32_t {
  Box,
  Sphere,
  Plane,
  Cylinder,
  Capsule,
};

struct MeshRenderer {
  Primitive primitive{Primitive::Box};
  glm::vec4 color{0.8f, 0.8f, 0.8f, 1.0f};
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

// Rendered from M3, authored and saved from M2.
struct PointLight {
  glm::vec3 color{1.0f, 1.0f, 1.0f};
  float intensity{10.0f};
  float range{10.0f};
};

struct SpotLight {
  glm::vec3 color{1.0f, 1.0f, 1.0f};
  float intensity{10.0f};
  float range{10.0f};
  float innerAngle{glm::radians(20.0f)};
  float outerAngle{glm::radians(30.0f)};
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
