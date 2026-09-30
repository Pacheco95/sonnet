#include <sonnet/renderer/Primitives.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>

using namespace sonnet::renderer;
using Catch::Approx;

namespace {

struct Case {
  std::string name;
  std::function<MeshData()> make;
  glm::vec3 halfExtents; // the mesh fits this box around the origin
  float maxU{1.0f};      // faces textured in metres, and a seam-crossing triangle, run past 1 for the repeat sampler
  float minU{0.0f};      // the hemisphere is textured outwards from its pole, so its coordinates are signed
};

void requireWellFormed(const MeshData &mesh, const Case &c) {
  INFO(c.name);
  REQUIRE(!mesh.vertices.empty());
  REQUIRE(mesh.indices.size() % 3 == 0);
  REQUIRE(mesh.triangleCount() > 0);
  for (const std::uint32_t index : mesh.indices) {
    REQUIRE(index < mesh.vertices.size());
  }
  for (const Vertex &vertex : mesh.vertices) {
    REQUIRE(glm::length(vertex.normal) == Approx(1.0f).epsilon(1e-4f));
    REQUIRE(std::abs(vertex.position.x) <= c.halfExtents.x + 1e-5f);
    REQUIRE(std::abs(vertex.position.y) <= c.halfExtents.y + 1e-5f);
    REQUIRE(std::abs(vertex.position.z) <= c.halfExtents.z + 1e-5f);
    REQUIRE(vertex.uv.x >= c.minU - 1e-5f);
    REQUIRE(vertex.uv.x <= c.maxU + 1e-5f);
  }
}

// Every triangle is counter-clockwise when seen from where its vertex normals point: the
// geometric normal agrees with the shading normals, and a degenerate triangle is not allowed.
void requireCounterClockwise(const MeshData &mesh, const Case &c) {
  INFO(c.name);
  for (std::size_t t = 0; t < mesh.indices.size(); t += 3) {
    const Vertex &a = mesh.vertices[mesh.indices[t]];
    const Vertex &b = mesh.vertices[mesh.indices[t + 1]];
    const Vertex &v = mesh.vertices[mesh.indices[t + 2]];
    const glm::vec3 geometric = glm::cross(b.position - a.position, v.position - a.position);
    REQUIRE(glm::length(geometric) > 1e-7f);
    const glm::vec3 shading = a.normal + b.normal + v.normal;
    REQUIRE(glm::dot(geometric, shading) > 0.0f);
  }
}

} // namespace

TEST_CASE("primitives are closed triangle lists with unit outward normals", "[renderer][primitives]") {
  const Case c =
      GENERATE(Case{"box", [] { return primitives::box({0.5f, 1.0f, 1.5f}); }, {0.5f, 1.0f, 1.5f}},
               Case{"sphere", [] { return primitives::sphere(2.0f, 12, 6); }, {2.0f, 2.0f, 2.0f}},
               Case{"plane", [] { return primitives::plane({4.0f, 2.0f}); }, {2.0f, 0.0f, 1.0f}},
               Case{"cylinder", [] { return primitives::cylinder(0.5f, 3.0f, 8); }, {0.5f, 1.5f, 0.5f}},
               Case{"capsule", [] { return primitives::capsule(0.5f, 3.0f, 8, 3); }, {0.5f, 1.5f, 0.5f}},
               Case{"cone", [] { return primitives::cone(0.5f, 3.0f, 8); }, {0.5f, 1.5f, 0.5f}},
               Case{"torus", [] { return primitives::torus(1.0f, 0.25f, 12, 6); }, {1.25f, 0.25f, 1.25f}},
               Case{"ramp", [] { return primitives::ramp({2.0f, 1.0f, 3.0f}); }, {1.0f, 0.5f, 1.5f}, 4.0f},
               Case{"stairs", [] { return primitives::stairs({2.0f, 1.0f, 3.0f}, 5); }, {1.0f, 0.5f, 1.5f}, 4.0f},
               Case{"hemisphere", [] { return primitives::hemisphere(2.0f, 12, 4); }, {2.0f, 1.0f, 2.0f}, 4.0f, -4.0f},
               Case{"arch", [] { return primitives::arch({2.0f, 3.0f, 0.5f}, 6); }, {1.0f, 1.5f, 0.25f}, 4.0f, -4.0f},
               Case{"icosphere", [] { return primitives::icosphere(2.0f, 2); }, {2.0f, 2.0f, 2.0f}, 1.5f});
  const MeshData mesh = c.make();
  requireWellFormed(mesh, c);
  requireCounterClockwise(mesh, c);
}

TEST_CASE("primitive triangle counts follow their parameters", "[renderer][primitives]") {
  REQUIRE(primitives::box().triangleCount() == 12);
  REQUIRE(primitives::plane().triangleCount() == 2);
  // Bands touching a pole hold one triangle per slice, the others two.
  REQUIRE(primitives::sphere(1.0f, 10, 5).triangleCount() == 10 * (5 - 2) * 2 + 10 * 2);
  REQUIRE(primitives::cylinder(1.0f, 1.0f, 10).triangleCount() == 10 * 2 + 10 * 2);
  REQUIRE(primitives::capsule(0.25f, 1.0f, 10, 4).triangleCount() == 2 * (10 * (4 - 1) * 2 + 10) + 10 * 2);
}

TEST_CASE("sphere vertices lie on the sphere and normals point outward", "[renderer][primitives]") {
  const MeshData mesh = primitives::sphere(1.5f, 16, 8);
  for (const Vertex &vertex : mesh.vertices) {
    REQUIRE(glm::length(vertex.position) == Approx(1.5f).epsilon(1e-4f));
    REQUIRE(glm::dot(vertex.normal, vertex.position) > 0.0f);
  }
}

TEST_CASE("plane faces +Y and box faces point away from the centre", "[renderer][primitives]") {
  for (const Vertex &vertex : primitives::plane().vertices) {
    REQUIRE(vertex.normal == glm::vec3{0.0f, 1.0f, 0.0f});
    REQUIRE(vertex.position.y == 0.0f);
  }
  for (const Vertex &vertex : primitives::box().vertices) {
    REQUIRE(glm::dot(vertex.normal, vertex.position) > 0.0f);
  }
}

TEST_CASE("the new primitives' triangle counts follow their parameters", "[renderer][primitives]") {
  REQUIRE(primitives::cone(1.0f, 1.0f, 10).triangleCount() == 10 + 10);
  REQUIRE(primitives::torus(1.0f, 0.25f, 12, 6).triangleCount() == 12 * 6 * 2);
  REQUIRE(primitives::ramp().triangleCount() == 2 + 2 + 2 + 1 + 1);
  REQUIRE(primitives::stairs({1.0f, 1.0f, 1.0f}, 4).triangleCount() == 4 + 4 * 8);
  REQUIRE(primitives::hemisphere(1.0f, 10, 4).triangleCount() == 10 + 10 * (4 - 1) * 2 + 10);
  // 20 faces, four times as many for each subdivision.
  REQUIRE(primitives::icosphere(1.0f, 0).triangleCount() == 20);
  REQUIRE(primitives::icosphere(1.0f, 3).triangleCount() == 20 * 64);
}

TEST_CASE("cone, hemisphere and icosphere keep to their curved surfaces", "[renderer][primitives]") {
  for (const Vertex &vertex : primitives::icosphere(1.5f, 2).vertices) {
    REQUIRE(glm::length(vertex.position) == Approx(1.5f).epsilon(1e-4f));
    REQUIRE(glm::dot(vertex.normal, vertex.position) > 0.0f);
  }
  // The hemisphere's bounds are centred: its sphere centre is half a radius below the origin.
  for (const Vertex &vertex : primitives::hemisphere(2.0f).vertices) {
    if (vertex.normal.y > -0.999f) {
      REQUIRE(glm::length(vertex.position - glm::vec3{0.0f, -1.0f, 0.0f}) == Approx(2.0f).epsilon(1e-4f));
    }
  }
  // Every side normal of the cone leans up by the slope: for radius 0.5 and height 1 that is 26.6 degrees from
  // vertical.
  for (const Vertex &vertex : primitives::cone(0.5f, 1.0f, 16).vertices) {
    if (vertex.normal.y > -0.999f) {
      REQUIRE(vertex.normal.y == Approx(0.5f / std::sqrt(1.25f)).epsilon(1e-4f));
    }
  }
}

TEST_CASE("ramp and stairs rise towards -Z and reach the full height there", "[renderer][primitives]") {
  for (const MeshData &mesh : {primitives::ramp({1.0f, 2.0f, 4.0f}), primitives::stairs({1.0f, 2.0f, 4.0f}, 4)}) {
    float frontTop = -100.0f;
    float backTop = -100.0f;
    for (const Vertex &vertex : mesh.vertices) {
      if (vertex.position.z > 1.9f) {
        frontTop = std::max(frontTop, vertex.position.y);
      }
      if (vertex.position.z < -1.9f) {
        backTop = std::max(backTop, vertex.position.y);
      }
    }
    REQUIRE(backTop == Approx(1.0f));
    REQUIRE(frontTop < backTop);
  }
}

TEST_CASE("the arch's opening is clear through its depth", "[renderer][primitives]") {
  const MeshData mesh = primitives::arch({2.0f, 3.0f, 0.5f}, 8);
  // No triangle covers the axis of the opening below the crown: a triangle spanning x = 0 at y = 0
  // would have a face there. The opening's radius is 0.6 and its crown at 3/2 - 0.4 = 1.1.
  for (std::size_t t = 0; t < mesh.indices.size(); t += 3) {
    const glm::vec3 a = mesh.vertices[mesh.indices[t]].position;
    const glm::vec3 b = mesh.vertices[mesh.indices[t + 1]].position;
    const glm::vec3 c = mesh.vertices[mesh.indices[t + 2]].position;
    const bool facing = a.z == b.z && b.z == c.z; // a front or back face lies in a plane of constant z
    if (!facing) {
      continue;
    }
    const auto inside = [&](glm::vec3 p) { return std::abs(p.x) < 0.6f - 1e-4f && p.y < 0.5f; };
    // The centroid of a wall triangle is never inside the straight part of the opening.
    REQUIRE(!inside((a + b + c) / 3.0f));
  }
}

TEST_CASE("the hemisphere's texture is laid out from its pole in metres", "[renderer][primitives]") {
  const MeshData mesh = primitives::hemisphere(2.0f, 16, 4);
  for (const Vertex &vertex : mesh.vertices) {
    if (vertex.normal.y < -0.999f) {
      continue; // the base
    }
    // The distance from the texture's origin is the distance along the surface from the pole.
    const float colatitude = std::acos(std::clamp(vertex.normal.y, -1.0f, 1.0f));
    REQUIRE(glm::length(vertex.uv) == Approx(2.0f * colatitude).margin(1e-4f));
  }
}

TEST_CASE("the arch's texture runs unbroken from its straight sides into the curve", "[renderer][primitives]") {
  // Size 2 x 3: the opening's radius is 0.6 and its spring line at y = 0.5. Where the left side meets the
  // curve the surface faces +X, and u is 0 on both the wall and the first segment of the curve.
  const MeshData mesh = primitives::arch({2.0f, 3.0f, 0.5f}, 8);
  int joined = 0;
  for (const Vertex &vertex : mesh.vertices) {
    if (std::abs(vertex.position.x + 0.6f) < 1e-4f && std::abs(vertex.position.y - 0.5f) < 1e-4f &&
        vertex.normal.x > 0.999f) {
      REQUIRE(vertex.uv.x == Approx(0.0f).margin(1e-4f));
      ++joined;
    }
  }
  REQUIRE(joined == 4); // two corners of the wall and two of the first curve segment
}
