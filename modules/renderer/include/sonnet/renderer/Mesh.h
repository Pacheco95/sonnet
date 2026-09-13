#pragma once

#include <sonnet/core/Handle.h>
#include <sonnet/core/Math.h>

#include <cstdint>
#include <vector>

namespace sonnet::renderer {

struct MeshTag {};
using MeshHandle = core::Handle<MeshTag>;

// Matches `Vertex` in shaders/sonnet.slang under scalar block layout: 32 bytes, no padding.
struct Vertex {
  glm::vec3 position;
  glm::vec3 normal;
  glm::vec2 uv;
};
static_assert(sizeof(Vertex) == 32);

// Mesh data on the CPU: counter-clockwise triangles with outward normals.
struct MeshData {
  std::vector<Vertex> vertices;
  std::vector<std::uint32_t> indices;

  [[nodiscard]] std::uint32_t triangleCount() const noexcept {
    return static_cast<std::uint32_t>(indices.size() / 3);
  }
};

} // namespace sonnet::renderer
