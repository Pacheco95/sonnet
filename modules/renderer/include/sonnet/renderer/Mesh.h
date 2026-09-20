#pragma once

#include <sonnet/core/Handle.h>
#include <sonnet/core/Math.h>

#include <cstdint>
#include <vector>

namespace sonnet::renderer {

struct MeshTag {};
using MeshHandle = core::Handle<MeshTag>;

// Matches `Vertex` in shaders/sonnet.slang under scalar block layout: 48 bytes, no padding.
struct Vertex {
  glm::vec3 position;
  glm::vec3 normal;
  glm::vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f}; // xyz in the normal's frame, w the bitangent sign
  glm::vec2 uv;
};
static_assert(sizeof(Vertex) == 48);

// A range of the index buffer drawn with one material slot.
struct Submesh {
  std::uint32_t firstIndex{0};
  std::uint32_t indexCount{0};
  std::uint32_t materialSlot{0};
};

// The joints that deform one vertex and their weights, which sum to one: indices into the skin's
// joint list, read by the skinning pass (docs/rendering.md, "Skinning"). Matches `SkinWeights` in
// shaders/skin.slang.
struct SkinWeights {
  glm::uvec4 joints{0u};
  glm::vec4 weights{0.0f};
};
static_assert(sizeof(SkinWeights) == 32);

struct Bounds {
  glm::vec3 min{0.0f};
  glm::vec3 max{0.0f};
};

// Mesh data on the CPU: counter-clockwise triangles with outward normals. Without submeshes the
// whole index range is one submesh with slot 0. A skinned mesh has one SkinWeights per vertex,
// its vertices in the bind pose; an unskinned one has none.
struct MeshData {
  std::vector<Vertex> vertices;
  std::vector<std::uint32_t> indices;
  std::vector<Submesh> submeshes;
  std::vector<SkinWeights> skin;

  [[nodiscard]] std::uint32_t triangleCount() const noexcept {
    return static_cast<std::uint32_t>(indices.size() / 3);
  }
  [[nodiscard]] Bounds bounds() const;
};

// Per-vertex tangents from the triangles' uv derivatives (Lengyel), for meshes that carry none.
void generateTangents(MeshData &mesh);

} // namespace sonnet::renderer
