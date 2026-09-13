#include <sonnet/renderer/Primitives.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <functional>
#include <string>

using namespace sonnet::renderer;
using Catch::Approx;

namespace {

struct Case {
  std::string name;
  std::function<MeshData()> make;
  glm::vec3 halfExtents; // the mesh fits this box around the origin
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
    REQUIRE(vertex.uv.x >= -1e-5f);
    REQUIRE(vertex.uv.x <= 1.0f + 1e-5f);
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
  const Case c = GENERATE(Case{"box", [] { return primitives::box({0.5f, 1.0f, 1.5f}); }, {0.5f, 1.0f, 1.5f}},
                          Case{"sphere", [] { return primitives::sphere(2.0f, 12, 6); }, {2.0f, 2.0f, 2.0f}},
                          Case{"plane", [] { return primitives::plane({4.0f, 2.0f}); }, {2.0f, 0.0f, 1.0f}},
                          Case{"cylinder", [] { return primitives::cylinder(0.5f, 3.0f, 8); }, {0.5f, 1.5f, 0.5f}},
                          Case{"capsule", [] { return primitives::capsule(0.5f, 3.0f, 8, 3); }, {0.5f, 1.5f, 0.5f}});
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
