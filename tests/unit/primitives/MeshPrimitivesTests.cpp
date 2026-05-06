#define GLM_ENABLE_EXPERIMENTAL
#include <catch2/catch_test_macros.hpp>

#include <sonnet/primitives/MeshPrimitives.hpp>

#include <algorithm>
#include <glm/gtc/epsilon.hpp>

static constexpr float kEps = 1e-4F;

static bool isUnitLength(glm::vec3 v) { return std::abs(glm::length(v) - 1.0F) < kEps; }

static bool allIndicesValid(const sonnet::renderer::CPUMesh& mesh) {
    const auto vcount = static_cast<uint32_t>(mesh.vertices.size());
    return std::ranges::all_of(mesh.indices, [vcount](uint32_t idx) { return idx < vcount; });
}

TEST_CASE("MeshPrimitives_MakeBox_VertexCount") {
    auto mesh = sonnet::primitives::makeBox({1.0F, 1.0F, 1.0F});
    REQUIRE(mesh.vertices.size() == 24); // 6 faces × 4 verts
    REQUIRE(mesh.indices.size() == 36);  // 6 faces × 2 tris × 3 verts
    REQUIRE(allIndicesValid(mesh));
}

TEST_CASE("MeshPrimitives_MakeBox_FlatNormalsPerFace") {
    auto mesh = sonnet::primitives::makeBox({1.0F, 1.0F, 1.0F});
    for (const auto& v : mesh.vertices) {
        REQUIRE(isUnitLength(v.normal));
    }
}

TEST_CASE("MeshPrimitives_MakeUVSphere_ValidIndices") {
    auto mesh = sonnet::primitives::makeUVSphere(12, 8);
    REQUIRE_FALSE(mesh.vertices.empty());
    REQUIRE_FALSE(mesh.indices.empty());
    REQUIRE(allIndicesValid(mesh));
}

TEST_CASE("MeshPrimitives_MakeUVSphere_NormalsAreUnitLength") {
    auto mesh = sonnet::primitives::makeUVSphere(12, 8);
    for (const auto& v : mesh.vertices) {
        REQUIRE(isUnitLength(v.normal));
    }
}

TEST_CASE("MeshPrimitives_MakeCylinder_ValidIndices") {
    auto mesh = sonnet::primitives::makeCylinder(0.5F, 2.0F, 16);
    REQUIRE_FALSE(mesh.vertices.empty());
    REQUIRE_FALSE(mesh.indices.empty());
    REQUIRE(allIndicesValid(mesh));
}

TEST_CASE("MeshPrimitives_MakePlane_FourVertices") {
    auto mesh = sonnet::primitives::makePlane({2.0F, 2.0F});
    REQUIRE(mesh.vertices.size() == 4);
    REQUIRE(mesh.indices.size() == 6);
    REQUIRE(std::ranges::all_of(mesh.vertices, [](const auto& v) {
        return std::abs(v.normal.x) < kEps && std::abs(v.normal.y - 1.0F) < kEps &&
               std::abs(v.normal.z) < kEps;
    }));
}

TEST_CASE("MeshPrimitives_MakeCapsule_ValidIndices") {
    auto mesh = sonnet::primitives::makeCapsule(0.5F, 1.0F, 16);
    REQUIRE_FALSE(mesh.vertices.empty());
    REQUIRE_FALSE(mesh.indices.empty());
    REQUIRE(allIndicesValid(mesh));
}
