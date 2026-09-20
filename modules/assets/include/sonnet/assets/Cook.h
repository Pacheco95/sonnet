#pragma once

#include <sonnet/renderer/Mesh.h>

#include <cstdint>
#include <span>

namespace sonnet::assets {

// Cooking turns imported data into what the player loads (docs/assets.md, "Cooking and
// export"). Meshes are the one kind that is reshaped rather than repackaged.

struct MeshCookStatistics {
  std::uint32_t verticesBefore{0};
  std::uint32_t verticesAfter{0}; // after welding
  float cacheMissesBefore{0.0f};  // average cache miss ratio, misses per triangle
  float cacheMissesAfter{0.0f};
};

// The post-transform vertex cache a cooked mesh is ordered for: the size tipsify fans around,
// and the size the ratios above are measured at.
constexpr std::uint32_t VertexCacheSize = 16;

// Welds vertices that are equal to the bit, reorders each submesh's triangles for the
// post-transform vertex cache (tipsify) and reorders the vertices by first use so the fetch
// runs forwards. The result draws the same geometry: the same triangles with the same vertex
// values, named in a different order.
[[nodiscard]] renderer::MeshData cookMesh(const renderer::MeshData &mesh, MeshCookStatistics *statistics = nullptr);

// Misses per triangle through a FIFO cache of `cacheSize` entries: 3.0 for an index buffer that
// never reuses anything, near 0.5 for a well-ordered closed mesh.
[[nodiscard]] float averageCacheMissRatio(std::span<const std::uint32_t> indices,
                                          std::uint32_t cacheSize = VertexCacheSize);

} // namespace sonnet::assets
