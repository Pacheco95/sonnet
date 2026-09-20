#include <sonnet/assets/Cook.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <set>
#include <tuple>
#include <vector>

using namespace sonnet;
using namespace sonnet::assets;
using renderer::MeshData;
using renderer::Vertex;

namespace {

// A grid of quads whose triangles each carry their own three vertices, emitted column by column
// so the index buffer reuses almost nothing: what an exporter that never welded would produce.
[[nodiscard]] MeshData unweldedGrid(std::uint32_t columns, std::uint32_t rows) {
  MeshData mesh;
  const auto corner = [&](std::uint32_t x, std::uint32_t y) {
    return Vertex{.position = {static_cast<float>(x), 0.0f, static_cast<float>(y)},
                  .normal = {0.0f, 1.0f, 0.0f},
                  .tangent = {1.0f, 0.0f, 0.0f, 1.0f},
                  .uv = {static_cast<float>(x), static_cast<float>(y)}};
  };
  const auto triangle = [&](std::array<std::pair<std::uint32_t, std::uint32_t>, 3> quadCorners) {
    for (const auto &[x, y] : quadCorners) {
      mesh.indices.push_back(static_cast<std::uint32_t>(mesh.vertices.size()));
      mesh.vertices.push_back(corner(x, y));
    }
  };
  for (std::uint32_t x = 0; x < columns; ++x) {
    for (std::uint32_t y = 0; y < rows; ++y) {
      triangle({{{x, y}, {x, y + 1}, {x + 1, y + 1}}});
      triangle({{{x, y}, {x + 1, y + 1}, {x + 1, y}}});
    }
  }
  return mesh;
}

// Every triangle as its three positions in winding order, rotated so the smallest comes first:
// two meshes with the same multiset draw the same geometry the same way round.
[[nodiscard]] std::multiset<std::array<float, 9>> triangles(const MeshData &mesh) {
  std::multiset<std::array<float, 9>> result;
  for (std::size_t slot = 0; slot + 2 < mesh.indices.size(); slot += 3) {
    std::array<glm::vec3, 3> corners{mesh.vertices[mesh.indices[slot]].position,
                                     mesh.vertices[mesh.indices[slot + 1]].position,
                                     mesh.vertices[mesh.indices[slot + 2]].position};
    const auto less = [](const glm::vec3 &a, const glm::vec3 &b) {
      return std::tie(a.x, a.y, a.z) < std::tie(b.x, b.y, b.z);
    };
    std::rotate(corners.begin(), std::ranges::min_element(corners, less), corners.end());
    result.insert({corners[0].x, corners[0].y, corners[0].z, corners[1].x, corners[1].y, corners[1].z, corners[2].x,
                   corners[2].y, corners[2].z});
  }
  return result;
}

} // namespace

TEST_CASE("cooking a mesh welds its vertices and keeps the geometry", "[assets][cook]") {
  const MeshData source = unweldedGrid(8, 8);
  REQUIRE(source.vertices.size() == 8 * 8 * 6); // three per triangle, nothing shared

  MeshCookStatistics statistics;
  const MeshData cooked = cookMesh(source, &statistics);

  // A grid of 8x8 quads has 9x9 distinct corners; welding finds exactly those.
  REQUIRE(statistics.verticesBefore == source.vertices.size());
  REQUIRE(cooked.vertices.size() == 9 * 9);
  REQUIRE(statistics.verticesAfter == cooked.vertices.size());
  REQUIRE(cooked.indices.size() == source.indices.size());
  REQUIRE(triangles(cooked) == triangles(source));
  REQUIRE(std::ranges::max(cooked.indices) < cooked.vertices.size());
}

TEST_CASE("cooking a mesh reorders it for the vertex cache", "[assets][cook]") {
  const MeshData source = unweldedGrid(24, 24);
  MeshCookStatistics statistics;
  const MeshData cooked = cookMesh(source, &statistics);

  // Unwelded, every one of the three indices of every triangle misses.
  REQUIRE(statistics.cacheMissesBefore == 3.0f);
  REQUIRE(statistics.cacheMissesAfter == averageCacheMissRatio(cooked.indices));
  // A closed grid cannot do better than about 0.5 misses per triangle; anything under 1.0 means
  // the ordering is doing its job rather than walking the mesh at random.
  REQUIRE(statistics.cacheMissesAfter < 1.0f);
  REQUIRE(triangles(cooked) == triangles(source));
}

TEST_CASE("cooking a mesh keeps its submeshes and their slots", "[assets][cook]") {
  MeshData source = unweldedGrid(4, 4);
  const auto half = static_cast<std::uint32_t>(source.indices.size() / 2);
  source.submeshes = {{.firstIndex = 0, .indexCount = half, .materialSlot = 0},
                      {.firstIndex = half, .indexCount = half, .materialSlot = 2}};

  const MeshData cooked = cookMesh(source);
  REQUIRE(cooked.submeshes.size() == 2);
  REQUIRE(cooked.submeshes[0].firstIndex == 0);
  REQUIRE(cooked.submeshes[0].indexCount == half);
  REQUIRE(cooked.submeshes[0].materialSlot == 0);
  REQUIRE(cooked.submeshes[1].firstIndex == half);
  REQUIRE(cooked.submeshes[1].indexCount == half);
  REQUIRE(cooked.submeshes[1].materialSlot == 2);
  REQUIRE(triangles(cooked) == triangles(source));
}

TEST_CASE("cooking a skinned mesh keeps each vertex with its weights", "[assets][cook]") {
  MeshData source = unweldedGrid(4, 4);
  source.skin.resize(source.vertices.size());
  for (std::size_t vertex = 0; vertex < source.vertices.size(); ++vertex) {
    // The joint follows the position, so a weight that moved to the wrong vertex shows up.
    const auto joint = static_cast<std::uint32_t>(source.vertices[vertex].position.x);
    source.skin[vertex] = {.joints = {joint, 0u, 0u, 0u}, .weights = {1.0f, 0.0f, 0.0f, 0.0f}};
  }

  const MeshData cooked = cookMesh(source);
  REQUIRE(cooked.skin.size() == cooked.vertices.size());
  for (std::size_t vertex = 0; vertex < cooked.vertices.size(); ++vertex) {
    REQUIRE(cooked.skin[vertex].joints.x == static_cast<std::uint32_t>(cooked.vertices[vertex].position.x));
  }
  REQUIRE(triangles(cooked) == triangles(source));
}

TEST_CASE("a mesh the cook cannot reshape comes back as it was", "[assets][cook]") {
  MeshData ragged = unweldedGrid(2, 2);
  ragged.indices.pop_back(); // no longer whole triangles
  REQUIRE(cookMesh(ragged).vertices.size() == ragged.vertices.size());

  MeshData outOfRange = unweldedGrid(2, 2);
  outOfRange.indices[0] = static_cast<std::uint32_t>(outOfRange.vertices.size());
  REQUIRE(cookMesh(outOfRange).vertices.size() == outOfRange.vertices.size());

  const MeshData empty;
  REQUIRE(cookMesh(empty).vertices.empty());
  REQUIRE(averageCacheMissRatio(empty.indices) == 0.0f);
}
