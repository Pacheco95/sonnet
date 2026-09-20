#include "AssetTestSupport.h"

#include <sonnet/assets/Importers.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/gtc/type_ptr.hpp>

#include <cstring>

using namespace sonnet;
using namespace sonnet::assets;
using Catch::Approx;

namespace sonnet::assets::test {

void writeSkinnedGltf(const std::filesystem::path &gltf) {
  using nlohmann::json;
  std::vector<std::byte> bin;
  json views = json::array();
  json accessors = json::array();
  // Appends the data as a buffer view and an accessor over it; returns the accessor's index.
  const auto add = [&](const void *data, std::size_t bytes, std::size_t count, const char *type, int component,
                       json extra = json::object()) {
    while (bin.size() % 4 != 0) {
      bin.push_back(std::byte{0});
    }
    views.push_back(json{{"buffer", 0}, {"byteOffset", bin.size()}, {"byteLength", bytes}});
    const auto *begin = static_cast<const std::byte *>(data);
    bin.insert(bin.end(), begin, begin + bytes);
    json accessor{{"bufferView", views.size() - 1}, {"componentType", component}, {"count", count}, {"type", type}};
    accessor.update(extra);
    accessors.push_back(std::move(accessor));
    return accessors.size() - 1;
  };
  constexpr int Float = 5126;
  constexpr int UnsignedByte = 5121;
  constexpr int UnsignedShort = 5123;

  const std::vector<float> positions{-0.5f, 0, 0, 0.5f, 0, 0, -0.5f, 2, 0, 0.5f, 2, 0};
  const std::vector<float> normals{0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
  const std::vector<std::uint8_t> joints{0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
  const std::vector<float> weights{1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
  const std::vector<std::uint16_t> indices{0, 1, 3, 0, 3, 2};
  const auto position = add(positions.data(), positions.size() * 4, 4, "VEC3", Float,
                            json{{"min", json::array({-0.5, 0, 0})}, {"max", json::array({0.5, 2, 0})}});
  const auto normal = add(normals.data(), normals.size() * 4, 4, "VEC3", Float);
  const auto joint = add(joints.data(), joints.size(), 4, "VEC4", UnsignedByte);
  const auto weight = add(weights.data(), weights.size() * 4, 4, "VEC4", Float);
  const auto index = add(indices.data(), indices.size() * 2, 6, "SCALAR", UnsignedShort);
  // Root at the origin, Tip one metre up.
  const glm::mat4 tipInverse = glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, -1.0f, 0.0f});
  std::vector<float> inverseBinds(32, 0.0f);
  const glm::mat4 identity{1.0f};
  std::memcpy(inverseBinds.data(), glm::value_ptr(identity), 64);
  std::memcpy(inverseBinds.data() + 16, glm::value_ptr(tipInverse), 64);
  const auto inverseBind = add(inverseBinds.data(), inverseBinds.size() * 4, 2, "MAT4", Float);

  const glm::quat quarter = glm::angleAxis(glm::radians(90.0f), glm::vec3{0.0f, 0.0f, 1.0f});
  const std::vector<float> second{0.0f, 1.0f};
  const std::vector<float> rotations{0, 0, 0, 1, quarter.x, quarter.y, quarter.z, quarter.w};
  const std::vector<float> stepTimes{0.0f, 0.5f};
  const std::vector<float> translations{0, 0, 0, 1, 0, 0};
  // Cubic spline: in-tangent, value, out-tangent per key.
  const std::vector<float> scales{0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 2, 2, 2, 0, 0, 0};
  const auto times = add(second.data(), 8, 2, "SCALAR", Float, json{{"min", {0.0}}, {"max", {1.0}}});
  const auto rotation = add(rotations.data(), rotations.size() * 4, 2, "VEC4", Float);
  const auto steps = add(stepTimes.data(), 8, 2, "SCALAR", Float, json{{"min", {0.0}}, {"max", {0.5}}});
  const auto translation = add(translations.data(), translations.size() * 4, 2, "VEC3", Float);
  const auto scale = add(scales.data(), scales.size() * 4, 6, "VEC3", Float);

  const std::string binName = gltf.stem().string() + ".bin";
  REQUIRE(core::writeFile(gltf.parent_path() / binName, bin).has_value());
  const json document{
      {"asset", {{"version", "2.0"}}},
      {"scene", 0},
      {"scenes", json::array({json{{"nodes", json::array({0})}}})},
      {"nodes", json::array({json{{"name", "Rig"}, {"children", json::array({1, 3})}},
                             json{{"name", "Root"}, {"children", json::array({2})}},
                             json{{"name", "Tip"}, {"translation", json::array({0.0, 1.0, 0.0})}},
                             json{{"name", "Strip"}, {"mesh", 0}, {"skin", 0}}})},
      {"meshes",
       json::array(
           {json{{"name", "StripMesh"},
                 {"primitives",
                  json::array(
                      {json{{"attributes",
                             {{"POSITION", position}, {"NORMAL", normal}, {"JOINTS_0", joint}, {"WEIGHTS_0", weight}}},
                            {"indices", index}}})}}})},
      {"skins", json::array({json{
                    {"name", "StripSkin"}, {"joints", json::array({1, 2})}, {"inverseBindMatrices", inverseBind}}})},
      {"animations",
       json::array({json{
           {"name", "Bend"},
           {"samplers", json::array({json{{"input", times}, {"output", rotation}},
                                     json{{"input", steps}, {"output", translation}, {"interpolation", "STEP"}},
                                     json{{"input", times}, {"output", scale}, {"interpolation", "CUBICSPLINE"}}})},
           {"channels", json::array({json{{"sampler", 0}, {"target", {{"node", 2}, {"path", "rotation"}}}},
                                     json{{"sampler", 1}, {"target", {{"node", 1}, {"path", "translation"}}}},
                                     json{{"sampler", 2}, {"target", {{"node", 2}, {"path", "scale"}}}}})}}})},
      {"buffers", json::array({json{{"uri", binName}, {"byteLength", bin.size()}}})},
      {"bufferViews", views},
      {"accessors", accessors},
  };
  REQUIRE(core::writeFile(gltf, document.dump(2)).has_value());
}

} // namespace sonnet::assets::test

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

TEST_CASE("a glTF file imports its skins, joint weights and animation clips by node path", "[assets][gltf]") {
  const std::filesystem::path directory = test::freshDirectory("sonnet_assets_gltf_skin");
  test::writeSkinnedGltf(directory / "rig.gltf");
  const auto imported = importGltf(directory / "rig.gltf");
  REQUIRE(imported.has_value());

  REQUIRE(imported->model.nodes.size() == 4);
  REQUIRE(imported->skinIndices == std::vector<std::int32_t>{-1, -1, -1, 0});
  REQUIRE(imported->meshes.size() == 1);
  const renderer::MeshData &mesh = imported->meshes[0].data;
  REQUIRE(mesh.skin.size() == 4);
  REQUIRE(mesh.skin[0].joints == glm::uvec4{0, 0, 0, 0});
  REQUIRE(mesh.skin[3].joints == glm::uvec4{1, 0, 0, 0});
  REQUIRE(mesh.skin[3].weights == glm::vec4{1.0f, 0.0f, 0.0f, 0.0f});

  REQUIRE(imported->skins.size() == 1);
  const Skin &skin = imported->skins[0].skin;
  REQUIRE(imported->skins[0].name == "StripSkin");
  REQUIRE(skin.joints == std::vector<std::string>{"Rig/Root", "Rig/Root/Tip"});
  REQUIRE(skin.inverseBindMatrices.size() == 2);
  REQUIRE(skin.inverseBindMatrices[1][3].y == Approx(-1.0f));

  REQUIRE(imported->animations.size() == 1);
  REQUIRE(imported->animations[0].name == "Bend");
  const AnimationClip &clip = imported->animations[0].clip;
  REQUIRE(clip.duration == Approx(1.0f));
  REQUIRE(clip.channels.size() == 3);
  REQUIRE(clip.channels[0].target == "Rig/Root/Tip");
  REQUIRE(clip.channels[0].path == AnimationPath::Rotation);
  REQUIRE(clip.channels[1].target == "Rig/Root");
  REQUIRE(clip.channels[1].interpolation == Interpolation::Step);
  REQUIRE(clip.channels[2].interpolation == Interpolation::CubicSpline);
  REQUIRE(clip.channels[2].values.size() == 6);
  std::filesystem::remove_all(directory);
}
