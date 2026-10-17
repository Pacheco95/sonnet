#pragma once

#include <sonnet/core/Handle.h>
#include <sonnet/core/Math.h>

#include <cstdint>
#include <vector>

namespace sonnet::renderer {

struct MeshTag {};
using MeshHandle = core::Handle<MeshTag>;

// Matches `Vertex` in shaders/sonnet.slang under scalar block layout: 60 bytes, no padding. `uv` is
// glTF's TEXCOORD_0 and `uv1` its TEXCOORD_1, zero where a mesh has no second set; a material's
// texture slot picks one (TextureTransform::texCoord). `color` is glTF's COLOR_0 as linear RGBA8,
// red in the lowest byte (glm::packUnorm4x8), white where a mesh has none (ADR-0031).
struct Vertex {
  glm::vec3 position;
  glm::vec3 normal;
  glm::vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f}; // xyz in the normal's frame, w the bitangent sign
  glm::vec2 uv;
  glm::vec2 uv1{0.0f, 0.0f};
  std::uint32_t color{0xFFFFFFFFu};
};
static_assert(sizeof(Vertex) == 60);

// A range of the index buffer drawn with one material slot. It may carry a second, coarser range
// of the same vertices that the far shadow cascades draw (ADR-0025): `shadowIndexCount` is 0 when
// there is none, `shadowFirstIndex` counts into the uploaded index buffer, which holds
// `MeshData::shadowIndices` after `MeshData::indices`, and `shadowError` is the worst distance the
// coarse surface is from the fine one as a fraction of the mesh's bounding radius.
struct Submesh {
  std::uint32_t firstIndex{0};
  std::uint32_t indexCount{0};
  std::uint32_t materialSlot{0};
  std::uint32_t shadowFirstIndex{0};
  std::uint32_t shadowIndexCount{0};
  float shadowError{0.0f};
};

// The joints that deform one vertex and their weights, which sum to one: indices into the skin's
// joint list, read by the skinning pass (docs/rendering.md, "Skinning"). Matches `SkinWeights` in
// shaders/skin.slang.
struct SkinWeights {
  glm::uvec4 joints{0u};
  glm::vec4 weights{0.0f};
};
static_assert(sizeof(SkinWeights) == 32);

// How far a morph target moves one vertex at weight 1, added to the bind pose before skinning
// (ADR-0023). Matches `MorphDelta` in shaders/skin.slang.
struct MorphDelta {
  glm::vec3 position{0.0f};
  glm::vec3 normal{0.0f};
  glm::vec3 tangent{0.0f};
};
static_assert(sizeof(MorphDelta) == 36);

// The most morph targets a mesh keeps; the importer drops the rest.
constexpr std::uint32_t MaxMorphTargets = 8;

struct Bounds {
  glm::vec3 min{0.0f};
  glm::vec3 max{0.0f};
};

// Mesh data on the CPU: counter-clockwise triangles with outward normals. Without submeshes the
// whole index range is one submesh with slot 0. A skinned mesh has one SkinWeights per vertex,
// its vertices in the bind pose; an unskinned one has none. A morphed mesh has `morphTargetCount`
// targets of one MorphDelta per vertex each, target after target.
struct MeshData {
  std::vector<Vertex> vertices;
  std::vector<std::uint32_t> indices;
  std::vector<std::uint32_t> shadowIndices; // the submeshes' coarse ranges, one after another (ADR-0025)
  std::vector<Submesh> submeshes;
  std::vector<SkinWeights> skin;
  std::uint32_t morphTargetCount{0};
  std::vector<MorphDelta> morphDeltas; // morphTargetCount * vertices.size()

  [[nodiscard]] std::uint32_t triangleCount() const noexcept {
    return static_cast<std::uint32_t>(indices.size() / 3);
  }
  [[nodiscard]] Bounds bounds() const;
};

// The most a coarse shadow range may deviate from its submesh, as a fraction of the mesh's bounding
// radius (half its largest extent), and the fewest triangles a submesh needs to be given one.
constexpr float ShadowRangeError = 0.10f;
// meshoptimizer's error is a quadric estimate and underestimates how far the fine vertices are from
// the coarse surface (icospheres measure 1.4 to 1.6 times it, and the ratio differs between
// platforms), so the range is simplified to ShadowRangeError over this and records the estimate
// times it.
constexpr float ShadowRangeErrorMargin = 1.75f;
constexpr std::uint32_t ShadowRangeMinTriangles = 64;

// Gives each submesh of an unskinned, unmorphed mesh a coarser range of its indices, simplified over
// the same vertices (meshoptimizer, borders locked) within ShadowRangeError, kept in `shadowIndices`
// only where it has at most half the triangles. Replaces any range the mesh had; call it once the
// fine indices are final (ADR-0025).
void buildShadowRange(MeshData &mesh);

// Per-vertex tangents from the triangles' uv derivatives (Lengyel), for meshes that carry none.
void generateTangents(MeshData &mesh);

} // namespace sonnet::renderer
