#pragma once

#include <sonnet/renderer/Camera.h>
#include <sonnet/renderer/Material.h>
#include <sonnet/renderer/Mesh.h>
#include <sonnet/renderer/Texture.h>

#include <sonnet/core/Handle.h>
#include <sonnet/core/Math.h>

#include <cstdint>
#include <span>

namespace sonnet::renderer {

struct EnvironmentTag {};
using EnvironmentHandle = core::Handle<EnvironmentTag>;

// What the renderer draws. The list is built by the layer above (`world`); the renderer never
// queries where it came from (docs/architecture.md). An item is one submesh with one material;
// an invalid material draws with the default one.
struct DrawItem {
  MeshHandle mesh{};
  std::uint32_t submesh{0};
  MaterialHandle material{};
  glm::mat4 transform{1.0f};
  glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f}; // multiplies the material's base colour
  std::uint32_t id{0};                     // written by the id pass for picking and the outline; 0 means none
  // A skinned draw deforms a mesh with skin weights by joints [firstJoint, firstJoint + jointCount)
  // of SceneView::joints; jointCount 0 draws the mesh as it is. `skinInstance`, non-zero, names
  // the deformed instance across frames so its skinned vertices keep their buffer; the draws of
  // one instance's submeshes share it and their joint range.
  std::uint32_t firstJoint{0};
  std::uint32_t jointCount{0};
  std::uint64_t skinInstance{0};
  // A morphed draw adds the weighted deltas of a mesh with morph targets to its vertices, before
  // skinning, using weights [firstMorphWeight, firstMorphWeight + morphWeightCount) of
  // SceneView::morphWeights (ADR-0023); it needs a skinInstance like a skinned one, and shares
  // it with a skin.
  std::uint32_t firstMorphWeight{0};
  std::uint32_t morphWeightCount{0};
};

// The sun: the one light that casts cascaded shadows.
struct DirectionalLight {
  glm::vec3 direction{-0.4f, -1.0f, -0.3f}; // towards where the light travels
  glm::vec3 color{1.0f, 0.96f, 0.9f};
  float intensity{3.0f};
};

enum class LightType : std::uint8_t {
  Point,
  Spot,
};

// A punctual light, clustered by the renderer (ADR-0008). Intensity is radiance at one metre.
struct Light {
  LightType type{LightType::Point};
  glm::vec3 position{0.0f};
  glm::vec3 color{1.0f};
  float intensity{10.0f};
  float range{10.0f};
  glm::vec3 direction{0.0f, 0.0f, -1.0f}; // spot lights: where the light travels
  float innerAngle{glm::radians(20.0f)};  // spot lights: full intensity inside
  float outerAngle{glm::radians(30.0f)};  // spot lights: nothing outside
  bool castsShadows{false}; // spot lights: asks for a shadow map, within RendererSettings::maxLocalShadows
};

// A world-space segment for debug drawing, such as a physics collider's outline.
struct DebugLine {
  glm::vec3 from{0.0f};
  glm::vec3 to{0.0f};
  glm::vec4 color{1.0f};
};

enum class ParticleBlend : std::uint8_t {
  Alpha,
  Additive,
};

// A GPU particle emitter for the frame (ADR-0023). The renderer keeps its particles between
// frames under `key`, simulates them in a compute pass and draws them as camera-facing quads
// after the blended meshes, depth-tested and not written. Each frame emits `rate * dt` particles
// (the fraction carried over), and `burst` more when the emitter first appears, into a ring of
// `maxParticles` slots that the oldest particles give way in. The same key, seed and sequence of
// `dt`s give the same particles.
struct ParticleEmitterItem {
  std::uint64_t key{0}; // names the emitter across frames; 0 draws nothing
  glm::mat4 transform{1.0f};
  std::uint32_t id{0}; // the owner's pick id, for tools that match items to entities
  std::uint32_t maxParticles{1000};
  float rate{50.0f};                    // particles per second
  std::uint32_t burst{0};               // particles emitted once, when the emitter is first seen
  glm::vec2 lifetime{1.0f, 2.0f};       // seconds, min and max
  glm::vec2 speed{1.0f, 2.0f};          // metres per second, min and max
  float coneAngle{glm::radians(30.0f)}; // half angle about the emitter's +Y
  glm::vec3 gravity{0.0f, -9.8f, 0.0f}; // in the space the particles simulate in
  float drag{0.0f};
  float sizeStart{0.1f}; // metres across
  float sizeEnd{0.1f};
  glm::vec4 colorStart{1.0f};
  glm::vec4 colorEnd{1.0f, 1.0f, 1.0f, 0.0f};
  TextureHandle texture{}; // invalid: a soft disc
  ParticleBlend blend{ParticleBlend::Alpha};
  bool local{false}; // particles move with the emitter instead of staying where they were born
  std::uint32_t seed{1};
  bool simulate{true}; // false draws the particles as they are
};

struct SceneView {
  Camera camera;
  DirectionalLight sun;
  bool hasSun{true};
  std::span<const Light> lights;
  // The image-based lighting and skybox; without one `ambient` lights the scene flatly.
  EnvironmentHandle environment{};
  float environmentIntensity{1.0f};
  glm::vec3 ambient{0.06f, 0.07f, 0.09f};
  float exposure{1.0f};
  float bloomStrength{0.04f};
  std::span<const DrawItem> draws;
  // The skinned draws' joint matrices, each from the mesh's bind pose into its object space.
  std::span<const glm::mat4> joints;
  // The morphed draws' target weights.
  std::span<const float> morphWeights;
  std::span<const DebugLine> debugLines; // drawn by addDebugLinePass
  std::span<const ParticleEmitterItem> particles;
  float deltaTime{0.0f}; // seconds the particles advance by this frame
};

} // namespace sonnet::renderer
