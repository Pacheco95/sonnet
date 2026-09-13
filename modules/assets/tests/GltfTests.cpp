#include "AssetTestSupport.h"

#include <sonnet/assets/Importers.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace sonnet;
using namespace sonnet::assets;
using Catch::Approx;

TEST_CASE("a glTF file imports its meshes, materials, images and node hierarchy", "[assets][gltf]") {
  const std::filesystem::path directory = test::freshDirectory("sonnet_assets_gltf");
  REQUIRE(core::writeFile(directory / "wood.png", test::encodePng({2, 2}, test::quadPixels())).has_value());
  test::writeBoxGltf(directory / "crate.gltf", "wood.png");

  const auto imported = importGltf(directory / "crate.gltf");
  REQUIRE(imported.has_value());
  REQUIRE(imported->meshes.size() == 1);
  const GltfMesh &mesh = imported->meshes[0];
  REQUIRE(mesh.name == "CrateMesh");
  REQUIRE(mesh.data.vertices.size() == 24);
  REQUIRE(mesh.data.indices.size() == 36);
  REQUIRE(mesh.data.submeshes.size() == 1);
  REQUIRE(mesh.data.submeshes[0].indexCount == 36);
  REQUIRE(mesh.materials == std::vector<std::int32_t>{0});
  // Tangents were generated: unit length, in the normal's plane.
  for (const renderer::Vertex &vertex : mesh.data.vertices) {
    REQUIRE(glm::length(glm::vec3{vertex.tangent}) == Approx(1.0f).epsilon(1e-4f));
    REQUIRE(std::abs(glm::dot(glm::vec3{vertex.tangent}, vertex.normal)) < 1e-4f);
  }

  REQUIRE(imported->materials.size() == 1);
  const GltfMaterial &material = imported->materials[0];
  REQUIRE(material.name == "Wood");
  REQUIRE(material.baseColorImage == 0);
  REQUIRE(material.normalImage == -1);
  REQUIRE(material.source.metallic == Approx(0.0f));
  REQUIRE(material.source.roughness == Approx(0.7f));
  REQUIRE(material.source.doubleSided);
  REQUIRE(material.source.wrap == renderer::TextureWrap::ClampToEdge);

  REQUIRE(imported->images.size() == 1);
  REQUIRE(imported->images[0].srgb); // read as base colour
  REQUIRE(!imported->images[0].bytes.empty());

  REQUIRE(imported->model.nodes.size() == 1);
  REQUIRE(imported->model.nodes[0].name == "Crate");
  REQUIRE(imported->model.nodes[0].parent == -1);
  REQUIRE(imported->model.nodes[0].position.z == Approx(3.0f));
  REQUIRE(imported->meshIndices == std::vector<std::int32_t>{0});

  REQUIRE(!importGltf(directory / "missing.gltf").has_value());
  std::filesystem::remove_all(directory);
}
