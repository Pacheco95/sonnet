#include "AssetTestSupport.h"

#include <sonnet/assets/Bundle.h>
#include <sonnet/assets/Cook.h>
#include <sonnet/assets/Importers.h>
#include <sonnet/core/File.h>
#include <sonnet/core/Log.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/gtc/type_ptr.hpp>
#include <meshoptimizer.h>
#include <spdlog/sinks/ostream_sink.h>

#include <array>
#include <cmath>
#include <cstring>
#include <sstream>

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
                       const json &extra = json::object()) {
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
  constexpr int floatType = 5126;
  constexpr int unsignedByteType = 5121;
  constexpr int unsignedShortType = 5123;

  const std::vector<float> positions{-0.5f, 0, 0, 0.5f, 0, 0, -0.5f, 2, 0, 0.5f, 2, 0};
  const std::vector<float> normals{0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
  const std::vector<std::uint8_t> joints{0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
  const std::vector<float> weights{1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
  const std::vector<std::uint16_t> indices{0, 1, 3, 0, 3, 2};
  const auto position = add(positions.data(), positions.size() * 4, 4, "VEC3", floatType,
                            json{{"min", json::array({-0.5, 0, 0})}, {"max", json::array({0.5, 2, 0})}});
  const auto normal = add(normals.data(), normals.size() * 4, 4, "VEC3", floatType);
  const auto joint = add(joints.data(), joints.size(), 4, "VEC4", unsignedByteType);
  const auto weight = add(weights.data(), weights.size() * 4, 4, "VEC4", floatType);
  const auto index = add(indices.data(), indices.size() * 2, 6, "SCALAR", unsignedShortType);
  // Root at the origin, Tip one metre up.
  const glm::mat4 tipInverse = glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, -1.0f, 0.0f});
  std::vector<float> inverseBinds(32, 0.0f);
  const glm::mat4 identity{1.0f};
  std::memcpy(inverseBinds.data(), glm::value_ptr(identity), 64);
  std::memcpy(inverseBinds.data() + 16, glm::value_ptr(tipInverse), 64);
  const auto inverseBind = add(inverseBinds.data(), inverseBinds.size() * 4, 2, "MAT4", floatType);

  const glm::quat quarter = glm::angleAxis(glm::radians(90.0f), glm::vec3{0.0f, 0.0f, 1.0f});
  const std::vector<float> second{0.0f, 1.0f};
  const std::vector<float> rotations{0, 0, 0, 1, quarter.x, quarter.y, quarter.z, quarter.w};
  const std::vector<float> stepTimes{0.0f, 0.5f};
  const std::vector<float> translations{0, 0, 0, 1, 0, 0};
  // Cubic spline: in-tangent, value, out-tangent per key.
  const std::vector<float> scales{0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 2, 2, 2, 0, 0, 0};
  const auto times = add(second.data(), 8, 2, "SCALAR", floatType, json{{"min", {0.0}}, {"max", {1.0}}});
  const auto rotation = add(rotations.data(), rotations.size() * 4, 2, "VEC4", floatType);
  const auto steps = add(stepTimes.data(), 8, 2, "SCALAR", floatType, json{{"min", {0.0}}, {"max", {0.5}}});
  const auto translation = add(translations.data(), translations.size() * 4, 2, "VEC3", floatType);
  const auto scale = add(scales.data(), scales.size() * 4, 6, "VEC3", floatType);

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

TEST_CASE("a transmissive glTF material imports as alpha-blended until refraction exists", "[assets][gltf]") {
  const std::filesystem::path directory = test::freshDirectory("sonnet_assets_gltf_transmission");
  REQUIRE(core::writeFile(directory / "wood.png", test::encodePng({2, 2}, test::quadPixels())).has_value());
  test::writeBoxGltf(directory / "glass.gltf", "wood.png");
  const auto bytes = core::readFile(directory / "glass.gltf");
  REQUIRE(bytes.has_value());
  nlohmann::json document =
      nlohmann::json::parse(std::string{reinterpret_cast<const char *>(bytes->data()), bytes->size()});
  // Required too, so every parse of the file has to accept it.
  document["extensionsUsed"] = {"KHR_materials_transmission"};
  document["extensionsRequired"] = {"KHR_materials_transmission"};
  document["materials"][0]["pbrMetallicRoughness"]["baseColorFactor"] = {1.0, 1.0, 1.0, 0.8};
  document["materials"][0]["extensions"]["KHR_materials_transmission"] = {{"transmissionFactor", 0.75}};
  REQUIRE(core::writeFile(directory / "glass.gltf", document.dump(2)).has_value());

  const auto imported = importGltf(directory / "glass.gltf");
  REQUIRE(imported.has_value());
  REQUIRE(imported->materials.size() == 1);
  const MaterialSource &source = imported->materials[0].source;
  REQUIRE(source.alphaMode == renderer::AlphaMode::Blend);
  REQUIRE(source.baseColor.a == Approx(0.2f));
  REQUIRE(source.baseColor.r == Approx(1.0f));
  REQUIRE(importGltfStructure(directory / "glass.gltf").has_value());
  REQUIRE(gltfDependencies(directory / "glass.gltf").has_value());

  // No transmission, or none let through, leaves the material as authored.
  document["materials"][0]["extensions"]["KHR_materials_transmission"]["transmissionFactor"] = 0.0;
  REQUIRE(core::writeFile(directory / "glass.gltf", document.dump(2)).has_value());
  const auto opaque = importGltf(directory / "glass.gltf");
  REQUIRE(opaque.has_value());
  REQUIRE(opaque->materials[0].source.alphaMode == renderer::AlphaMode::Opaque);
  REQUIRE(opaque->materials[0].source.baseColor.a == Approx(0.8f));
  std::filesystem::remove_all(directory);
}

TEST_CASE("a glTF material's KHR_materials_emissive_strength scales its emissive", "[assets][gltf]") {
  const std::filesystem::path directory = test::freshDirectory("sonnet_assets_gltf_emissive_strength");
  REQUIRE(core::writeFile(directory / "wood.png", test::encodePng({2, 2}, test::quadPixels())).has_value());
  test::writeBoxGltf(directory / "lamp.gltf", "wood.png");
  const auto bytes = core::readFile(directory / "lamp.gltf");
  REQUIRE(bytes.has_value());
  nlohmann::json document =
      nlohmann::json::parse(std::string{reinterpret_cast<const char *>(bytes->data()), bytes->size()});
  document["extensionsUsed"] = {"KHR_materials_emissive_strength"};
  document["materials"][0]["emissiveFactor"] = {1.0, 0.5, 0.0};
  document["materials"][0]["extensions"]["KHR_materials_emissive_strength"] = {{"emissiveStrength", 4.0}};
  REQUIRE(core::writeFile(directory / "lamp.gltf", document.dump(2)).has_value());

  const auto imported = importGltf(directory / "lamp.gltf");
  REQUIRE(imported.has_value());
  REQUIRE(imported->materials.size() == 1);
  const glm::vec3 emissive = imported->materials[0].source.emissive;
  REQUIRE(emissive.r == Approx(4.0f));
  REQUIRE(emissive.g == Approx(2.0f));
  REQUIRE(emissive.b == Approx(0.0f));
  std::filesystem::remove_all(directory);
}

TEST_CASE("a glTF material's KHR_texture_transform imports per texture slot", "[assets][gltf]") {
  const std::filesystem::path directory = test::freshDirectory("sonnet_assets_gltf_texture_transform");
  REQUIRE(core::writeFile(directory / "wood.png", test::encodePng({2, 2}, test::quadPixels())).has_value());
  test::writeBoxGltf(directory / "tiled.gltf", "wood.png");
  const auto bytes = core::readFile(directory / "tiled.gltf");
  REQUIRE(bytes.has_value());
  nlohmann::json document =
      nlohmann::json::parse(std::string{reinterpret_cast<const char *>(bytes->data()), bytes->size()});
  document["extensionsUsed"] = {"KHR_texture_transform"};
  nlohmann::json &material = document["materials"][0];
  material["pbrMetallicRoughness"]["baseColorTexture"]["extensions"]["KHR_texture_transform"] = {
      {"offset", {0.5, -0.25}}, {"rotation", 1.25}, {"scale", {4.0, 2.0}}};
  // A slot that names only some of the fields keeps the identity in the others.
  material["normalTexture"] = {{"index", 0}, {"extensions", {{"KHR_texture_transform", {{"scale", {-1.0, 1.0}}}}}}};
  REQUIRE(core::writeFile(directory / "tiled.gltf", document.dump(2)).has_value());

  const auto imported = importGltf(directory / "tiled.gltf");
  REQUIRE(imported.has_value());
  const MaterialSource &source = imported->materials[0].source;
  REQUIRE(source.baseColorTransform.offset == glm::vec2{0.5f, -0.25f});
  REQUIRE(source.baseColorTransform.rotation == Approx(1.25f));
  REQUIRE(source.baseColorTransform.scale == glm::vec2{4.0f, 2.0f});
  REQUIRE(source.normalTransform ==
          renderer::TextureTransform{.offset = {0.0f, 0.0f}, .rotation = 0.0f, .scale = {-1.0f, 1.0f}});
  REQUIRE(source.metallicRoughnessTransform == renderer::TextureTransform{});
  REQUIRE(source.occlusionTransform == renderer::TextureTransform{});
  REQUIRE(source.emissiveTransform == renderer::TextureTransform{});
  std::filesystem::remove_all(directory);
}

TEST_CASE("a glTF primitive's TEXCOORD_1 imports, and each slot reads the set it names", "[assets][gltf]") {
  const std::filesystem::path directory = test::freshDirectory("sonnet_assets_gltf_second_uv_set");
  REQUIRE(core::writeFile(directory / "wood.png", test::encodePng({2, 2}, test::quadPixels())).has_value());
  test::writeBoxGltf(directory / "baked.gltf", "wood.png");
  const auto bytes = core::readFile(directory / "baked.gltf");
  REQUIRE(bytes.has_value());
  nlohmann::json document =
      nlohmann::json::parse(std::string{reinterpret_cast<const char *>(bytes->data()), bytes->size()});
  // The second set from a buffer of its own: each vertex's index, so every vertex is told apart.
  const std::size_t count = document["accessors"][0]["count"].get<std::size_t>();
  std::vector<float> secondSet;
  for (std::size_t i = 0; i < count; ++i) {
    secondSet.insert(secondSet.end(), {static_cast<float>(i), 0.5f});
  }
  REQUIRE(core::writeFile(directory / "uv1.bin", std::as_bytes(std::span{secondSet})).has_value());
  document["buffers"].push_back({{"uri", "uv1.bin"}, {"byteLength", secondSet.size() * sizeof(float)}});
  document["bufferViews"].push_back(
      {{"buffer", 1}, {"byteOffset", 0}, {"byteLength", secondSet.size() * sizeof(float)}});
  document["accessors"].push_back({{"bufferView", document["bufferViews"].size() - 1},
                                   {"componentType", 5126},
                                   {"count", count},
                                   {"type", "VEC2"}});
  document["meshes"][0]["primitives"][0]["attributes"]["TEXCOORD_1"] = document["accessors"].size() - 1;
  // Occlusion on the second set; the base colour names it too, but its KHR_texture_transform
  // takes it back to the first.
  document["extensionsUsed"] = {"KHR_texture_transform"};
  nlohmann::json &material = document["materials"][0];
  material["occlusionTexture"] = {{"index", 0}, {"texCoord", 1}};
  material["pbrMetallicRoughness"]["baseColorTexture"] = {
      {"index", 0}, {"texCoord", 1}, {"extensions", {{"KHR_texture_transform", {{"texCoord", 0}}}}}};
  material["emissiveTexture"] = {{"index", 0}, {"extensions", {{"KHR_texture_transform", {{"texCoord", 1}}}}}};
  REQUIRE(core::writeFile(directory / "baked.gltf", document.dump(2)).has_value());

  const auto imported = importGltf(directory / "baked.gltf");
  REQUIRE(imported.has_value());
  const std::vector<renderer::Vertex> &vertices = imported->meshes[0].data.vertices;
  REQUIRE(vertices.size() == count);
  for (std::size_t i = 0; i < count; ++i) {
    REQUIRE(vertices[i].uv1 == glm::vec2{static_cast<float>(i), 0.5f});
  }
  const MaterialSource &source = imported->materials[0].source;
  REQUIRE(source.occlusionTransform == renderer::TextureTransform{.texCoord = 1});
  REQUIRE(source.baseColorTransform == renderer::TextureTransform{});
  REQUIRE(source.emissiveTransform == renderer::TextureTransform{.texCoord = 1});
  REQUIRE(source.normalTransform.texCoord == 0);

  // Without the attribute, the second set is zeros.
  test::writeBoxGltf(directory / "plain.gltf", "wood.png");
  const auto plain = importGltf(directory / "plain.gltf");
  REQUIRE(plain.has_value());
  for (const renderer::Vertex &vertex : plain->meshes[0].data.vertices) {
    REQUIRE(vertex.uv1 == glm::vec2{0.0f, 0.0f});
  }
  std::filesystem::remove_all(directory);
}

TEST_CASE("a glTF primitive quantized by KHR_mesh_quantization imports within quantization error", "[assets][gltf]") {
  using nlohmann::json;
  const std::filesystem::path directory = test::freshDirectory("sonnet_assets_gltf_quantized");
  // A tilted quad in floats, quantized the way gltfpack does: positions as int16 in a grid whose
  // scale and offset go to the node, normals and tangents as normalized int8, UVs as normalized
  // uint16. Each element is padded to four bytes, as the specification asks of vertex attributes.
  const std::array<glm::vec3, 4> positions{glm::vec3{-1.5f, 0.25f, -2.0f}, glm::vec3{2.5f, 0.25f, -2.0f},
                                           glm::vec3{-1.5f, 1.75f, 3.0f}, glm::vec3{2.5f, 1.75f, 3.0f}};
  const glm::vec3 normal = glm::normalize(glm::vec3{0.0f, 1.0f, -0.3f});
  const glm::vec4 tangent{1.0f, 0.0f, 0.0f, -1.0f};
  const std::array<glm::vec2, 4> uvs{glm::vec2{0.1f, 0.9f}, glm::vec2{0.7f, 0.9f}, glm::vec2{0.1f, 0.2f},
                                     glm::vec2{0.7f, 0.2f}};
  const glm::vec3 offset = positions[0];
  const glm::vec3 step = (positions[3] - positions[0]) / 32767.0f;
  const auto snorm8 = [](float value) { return static_cast<std::int8_t>(std::round(value * 127.0f)); };
  const auto unorm16 = [](float value) { return static_cast<std::uint16_t>(std::round(value * 65535.0f)); };
  std::vector<std::int16_t> quantizedPositions; // x, y, z, padding
  std::vector<std::int8_t> quantizedNormals;    // x, y, z, padding
  std::vector<std::int8_t> quantizedTangents;
  std::vector<std::uint16_t> quantizedUvs;
  for (std::size_t i = 0; i < positions.size(); ++i) {
    const glm::vec3 grid = glm::round((positions[i] - offset) / step);
    quantizedPositions.insert(
        quantizedPositions.end(),
        {static_cast<std::int16_t>(grid.x), static_cast<std::int16_t>(grid.y), static_cast<std::int16_t>(grid.z), 0});
    quantizedNormals.insert(quantizedNormals.end(), {snorm8(normal.x), snorm8(normal.y), snorm8(normal.z), 0});
    quantizedTangents.insert(quantizedTangents.end(),
                             {snorm8(tangent.x), snorm8(tangent.y), snorm8(tangent.z), snorm8(tangent.w)});
    quantizedUvs.insert(quantizedUvs.end(), {unorm16(uvs[i].x), unorm16(uvs[i].y)});
  }
  const std::vector<std::uint16_t> indices{0, 2, 1, 1, 2, 3};

  std::vector<std::byte> bin;
  json views = json::array();
  json accessors = json::array();
  const auto add = [&](std::span<const std::byte> data, std::size_t stride, int component, const char *type,
                       bool normalized) {
    views.push_back(json{{"buffer", 0}, {"byteOffset", bin.size()}, {"byteLength", data.size()}});
    if (stride != 0) {
      views.back()["byteStride"] = stride;
    }
    bin.insert(bin.end(), data.begin(), data.end());
    json accessor{{"bufferView", views.size() - 1},
                  {"componentType", component},
                  {"count", stride != 0 ? data.size() / stride : indices.size()},
                  {"type", type}};
    if (normalized) {
      accessor["normalized"] = true;
    }
    accessors.push_back(std::move(accessor));
    return accessors.size() - 1;
  };
  constexpr int byteType = 5120;
  constexpr int shortType = 5122;
  constexpr int unsignedShortType = 5123;
  const auto position = add(std::as_bytes(std::span{quantizedPositions}), 8, shortType, "VEC3", false);
  accessors[position]["min"] = {0, 0, 0};
  accessors[position]["max"] = {32767, 32767, 32767};
  const auto normalAccessor = add(std::as_bytes(std::span{quantizedNormals}), 4, byteType, "VEC3", true);
  const auto tangentAccessor = add(std::as_bytes(std::span{quantizedTangents}), 4, byteType, "VEC4", true);
  const auto uv = add(std::as_bytes(std::span{quantizedUvs}), 4, unsignedShortType, "VEC2", true);
  const auto index = add(std::as_bytes(std::span{indices}), 0, unsignedShortType, "SCALAR", false);
  REQUIRE(core::writeFile(directory / "packed.bin", bin).has_value());
  const json document{
      {"asset", {{"version", "2.0"}}},
      {"extensionsUsed", {"KHR_mesh_quantization"}},
      {"extensionsRequired", {"KHR_mesh_quantization"}},
      {"scene", 0},
      {"scenes", json::array({json{{"nodes", json::array({0})}}})},
      {"nodes", json::array({json{{"name", "Packed"},
                                  {"mesh", 0},
                                  {"translation", {offset.x, offset.y, offset.z}},
                                  {"scale", {step.x, step.y, step.z}}}})},
      {"meshes", json::array({json{{"name", "PackedMesh"},
                                   {"primitives", json::array({json{{"attributes",
                                                                     {{"POSITION", position},
                                                                      {"NORMAL", normalAccessor},
                                                                      {"TANGENT", tangentAccessor},
                                                                      {"TEXCOORD_0", uv}}},
                                                                    {"indices", index}}})}}})},
      {"buffers", json::array({json{{"uri", "packed.bin"}, {"byteLength", bin.size()}}})},
      {"bufferViews", views},
      {"accessors", accessors},
  };
  REQUIRE(core::writeFile(directory / "packed.gltf", document.dump(2)).has_value());

  const auto imported = importGltf(directory / "packed.gltf");
  REQUIRE(imported.has_value());
  REQUIRE(importGltfStructure(directory / "packed.gltf").has_value());
  REQUIRE(gltfDependencies(directory / "packed.gltf").has_value());
  REQUIRE(imported->model.nodes.size() == 1);
  const ModelNode &node = imported->model.nodes[0];
  const std::vector<renderer::Vertex> &vertices = imported->meshes[0].data.vertices;
  REQUIRE(vertices.size() == positions.size());
  REQUIRE(imported->meshes[0].data.indices == std::vector<std::uint32_t>{0, 2, 1, 1, 2, 3});
  // Half a grid step for positions, half a quantum for the rest, and float rounding on top.
  const glm::vec3 positionError = step * 0.5f + 1e-5f;
  for (std::size_t i = 0; i < vertices.size(); ++i) {
    const glm::vec3 restored = node.position + node.scale * vertices[i].position;
    REQUIRE(glm::all(glm::lessThanEqual(glm::abs(restored - positions[i]), positionError)));
    REQUIRE(glm::all(glm::lessThanEqual(glm::abs(vertices[i].normal - normal), glm::vec3{0.5f / 127.0f + 1e-6f})));
    REQUIRE(glm::all(glm::lessThanEqual(glm::abs(vertices[i].tangent - tangent), glm::vec4{0.5f / 127.0f + 1e-6f})));
    REQUIRE(glm::all(glm::lessThanEqual(glm::abs(vertices[i].uv - uvs[i]), glm::vec2{0.5f / 65535.0f + 1e-6f})));
  }
  std::filesystem::remove_all(directory);
}

TEST_CASE("a glTF file names the extensions it uses that the importer does not read", "[assets][gltf]") {
  const std::filesystem::path directory = test::freshDirectory("sonnet_assets_gltf_extensions");
  REQUIRE(core::writeFile(directory / "wood.png", test::encodePng({2, 2}, test::quadPixels())).has_value());
  test::writeBoxGltf(directory / "coated.gltf", "wood.png");
  const auto bytes = core::readFile(directory / "coated.gltf");
  REQUIRE(bytes.has_value());
  nlohmann::json document =
      nlohmann::json::parse(std::string{reinterpret_cast<const char *>(bytes->data()), bytes->size()});
  document["extensionsUsed"] = {"KHR_texture_transform", "KHR_materials_clearcoat", "EXT_made_up"};
  REQUIRE(core::writeFile(directory / "coated.gltf", document.dump(2)).has_value());

  // Used but not read: imported, with one warning naming the file and every unread extension.
  std::ostringstream log;
  const auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(log, true);
  core::Log::addSink(sink);
  const auto imported = importGltf(directory / "coated.gltf");
  core::Log::removeSink(sink);
  REQUIRE(imported.has_value());
  const std::string warned = log.str();
  REQUIRE(warned.find("coated.gltf: uses KHR_materials_clearcoat, EXT_made_up, which the importer does not read") !=
          std::string::npos);
  REQUIRE(warned.find("KHR_texture_transform") == std::string::npos);
  REQUIRE(warned.find("uses") == warned.rfind("uses"));

  // Required but not read: refused, by every parse, naming the extension.
  document["extensionsRequired"] = {"KHR_texture_transform", "KHR_materials_clearcoat"};
  REQUIRE(core::writeFile(directory / "coated.gltf", document.dump(2)).has_value());
  const auto refused = importGltf(directory / "coated.gltf");
  REQUIRE(!refused.has_value());
  REQUIRE(refused.error().message.find("requires KHR_materials_clearcoat, which the importer does not read") !=
          std::string::npos);
  REQUIRE(!importGltfStructure(directory / "coated.gltf").has_value());
  REQUIRE(!gltfDependencies(directory / "coated.gltf").has_value());

  // The same from a .glb, whose JSON is its first chunk.
  const std::string json = document.dump();
  const std::string padded = json + std::string((4 - json.size() % 4) % 4, ' ');
  std::vector<std::byte> glb(20 + padded.size());
  const std::array<std::uint32_t, 5> header{0x46546C67u, 2u, static_cast<std::uint32_t>(glb.size()),
                                            static_cast<std::uint32_t>(padded.size()), 0x4E4F534Au};
  std::memcpy(glb.data(), header.data(), sizeof(header));
  std::memcpy(glb.data() + 20, padded.data(), padded.size());
  REQUIRE(core::writeFile(directory / "coated.glb", glb).has_value());
  const auto refusedGlb = importGltfStructure(directory / "coated.glb");
  REQUIRE(!refusedGlb.has_value());
  REQUIRE(refusedGlb.error().message.find("requires KHR_materials_clearcoat") != std::string::npos);
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
  REQUIRE(clip.channels[2].values.size() ==
          6 * static_cast<std::size_t>(width(clip.channels[2]))); // three values for each of two keys
  std::filesystem::remove_all(directory);
}

namespace {

// One triangle on node "Face" whose single morph target pushes every vertex a metre along +Z, a
// mesh default weight of a quarter and a clip "Smile" taking the weight from 0 to 1 in a second.
constexpr const char *MorphModel =
    R"json({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"name":"Face","mesh":0}],"meshes":[{"name":"FaceMesh","weights":[0.25],"primitives":[{"attributes":{"POSITION":0},"indices":1,"targets":[{"POSITION":2}]}]}],"animations":[{"name":"Smile","samplers":[{"input":3,"output":4}],"channels":[{"sampler":0,"target":{"node":0,"path":"weights"}}]}],"buffers":[{"uri":"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAEAAAACAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAgD8AAAAAAACAPw==","byteLength":100}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":12},{"buffer":0,"byteOffset":48,"byteLength":36},{"buffer":0,"byteOffset":84,"byteLength":8},{"buffer":0,"byteOffset":92,"byteLength":8}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5125,"count":3,"type":"SCALAR"},{"bufferView":2,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":3,"componentType":5126,"count":2,"type":"SCALAR","min":[0],"max":[1]},{"bufferView":4,"componentType":5126,"count":2,"type":"SCALAR"}]})json";

} // namespace

TEST_CASE("a glTF file imports morph targets, their default weights and weight channels", "[assets][gltf]") {
  const std::filesystem::path directory = test::freshDirectory("sonnet_assets_gltf_morph");
  REQUIRE(core::writeFile(directory / "face.gltf", std::as_bytes(std::span{MorphModel, std::strlen(MorphModel)}))
              .has_value());
  const auto imported = importGltf(directory / "face.gltf");
  REQUIRE(imported.has_value());

  REQUIRE(imported->meshes.size() == 1);
  const renderer::MeshData &mesh = imported->meshes[0].data;
  REQUIRE(mesh.morphTargetCount == 1);
  REQUIRE(mesh.morphDeltas.size() == mesh.vertices.size());
  REQUIRE(mesh.morphDeltas[1].position == glm::vec3{0.0f, 0.0f, 1.0f});
  REQUIRE(mesh.morphDeltas[1].normal == glm::vec3{0.0f});
  REQUIRE(imported->model.nodes[0].morphWeights == std::vector<float>{0.25f});

  REQUIRE(imported->animations.size() == 1);
  const AnimationChannel &channel = imported->animations[0].clip.channels.at(0);
  REQUIRE(channel.path == AnimationPath::Weights);
  REQUIRE(channel.weightCount == 1);
  std::array<float, 1> weight{};
  sample(channel, 0.25f, weight);
  REQUIRE(weight[0] == Approx(0.25f));

  // Through a cooked mesh payload: the targets survive a round trip, the vertices reordered.
  const renderer::MeshData cooked = cookMesh(mesh);
  REQUIRE(cooked.morphTargetCount == 1);
  REQUIRE(cooked.morphDeltas.size() == cooked.vertices.size());
  for (std::size_t v = 0; v < cooked.vertices.size(); ++v) {
    REQUIRE(cooked.morphDeltas[v].position.z == Approx(1.0f));
  }
  const auto decoded = decodeMesh(encodeMesh(cooked));
  REQUIRE(decoded.has_value());
  REQUIRE(decoded->morphTargetCount == 1);
  REQUIRE(decoded->morphDeltas.size() == cooked.morphDeltas.size());
  std::filesystem::remove_all(directory);
}

namespace {

// How writeQuadGltf stores its index buffer view.
enum class IndexCompression : std::uint8_t {
  None,
  Triangles,
  Indices
};

// A quad folded along its diagonal, positions and normals interleaved in one view and 16-bit
// indices in another. Compressed, both views are EXT_meshopt_compression data in buffer 0 over a
// fallback buffer 1 that has no data: the vertices with the exponential filter, the indices in the
// mode `indexMode` names. Uncompressed (`None`), the same views in a plain buffer.
void writeQuadGltf(const std::filesystem::path &gltf, IndexCompression indexMode) {
  using nlohmann::json;
  constexpr std::size_t vertexCount = 4;
  constexpr std::size_t stride = 24;
  const std::vector<float> vertices{-1.0f, 0.0f, -1.0f, 0.0f,   1.0f,  0.0f,    1.0f, 0.0f, -1.0f, 0.0f,   1.0f, 0.0f,
                                    -1.0f, 0.5f, 1.0f,  -0.25f, 0.75f, -0.375f, 1.0f, 2.5f, 1.0f,  0.125f, 0.5f, 0.0f};
  const std::vector<std::uint16_t> indices16{0, 2, 1, 1, 2, 3};
  const std::vector<std::uint32_t> indices32(indices16.begin(), indices16.end()); // what the encoder takes
  const std::size_t vertexBytes = vertexCount * stride;
  const std::size_t indexBytes = indices16.size() * 2;
  const bool compressed = indexMode != IndexCompression::None;

  std::vector<std::byte> bin;
  json views = json::array();
  const auto append = [&](std::span<const std::byte> bytes) {
    const std::size_t offset = bin.size();
    bin.insert(bin.end(), bytes.begin(), bytes.end());
    while (bin.size() % 4 != 0) {
      bin.push_back(std::byte{0});
    }
    return offset;
  };
  if (!compressed) {
    const std::size_t vertexOffset = append(std::as_bytes(std::span{vertices}));
    const std::size_t indexOffset = append(std::as_bytes(std::span{indices16}));
    views.push_back(
        json{{"buffer", 0}, {"byteOffset", vertexOffset}, {"byteLength", vertexBytes}, {"byteStride", stride}});
    views.push_back(json{{"buffer", 0}, {"byteOffset", indexOffset}, {"byteLength", indexBytes}});
  } else {
    // The exponential filter's input is the floats, its output what the vertex codec compresses.
    std::vector<std::uint32_t> filtered(vertices.size());
    meshopt_encodeFilterExp(filtered.data(), vertexCount, stride, 24, vertices.data(), meshopt_EncodeExpSeparate);
    std::vector<unsigned char> vertexCodec(meshopt_encodeVertexBufferBound(vertexCount, stride));
    // Version 0, the one EXT_meshopt_compression allows.
    vertexCodec.resize(meshopt_encodeVertexBufferLevel(vertexCodec.data(), vertexCodec.size(), filtered.data(),
                                                       vertexCount, stride, 2, 0));
    const bool sequence = indexMode == IndexCompression::Indices;
    std::vector<unsigned char> indexCodec(sequence ? meshopt_encodeIndexSequenceBound(indices32.size(), vertexCount)
                                                   : meshopt_encodeIndexBufferBound(indices32.size(), vertexCount));
    indexCodec.resize(
        sequence ? meshopt_encodeIndexSequence(indexCodec.data(), indexCodec.size(), indices32.data(), indices32.size())
                 : meshopt_encodeIndexBuffer(indexCodec.data(), indexCodec.size(), indices32.data(), indices32.size()));
    REQUIRE(!vertexCodec.empty());
    REQUIRE(!indexCodec.empty());
    const std::size_t vertexOffset = append(std::as_bytes(std::span{vertexCodec}));
    const std::size_t indexOffset = append(std::as_bytes(std::span{indexCodec}));
    views.push_back(json{{"buffer", 1},
                         {"byteOffset", 0},
                         {"byteLength", vertexBytes},
                         {"byteStride", stride},
                         {"extensions",
                          {{"EXT_meshopt_compression",
                            {{"buffer", 0},
                             {"byteOffset", vertexOffset},
                             {"byteLength", vertexCodec.size()},
                             {"byteStride", stride},
                             {"count", vertexCount},
                             {"mode", "ATTRIBUTES"},
                             {"filter", "EXPONENTIAL"}}}}}});
    views.push_back(json{{"buffer", 1},
                         {"byteOffset", vertexBytes},
                         {"byteLength", indexBytes},
                         {"extensions",
                          {{"EXT_meshopt_compression",
                            {{"buffer", 0},
                             {"byteOffset", indexOffset},
                             {"byteLength", indexCodec.size()},
                             {"byteStride", 2},
                             {"count", indices16.size()},
                             {"mode", sequence ? "INDICES" : "TRIANGLES"}}}}}});
  }
  const std::string binName = gltf.stem().string() + ".bin";
  REQUIRE(core::writeFile(gltf.parent_path() / binName, bin).has_value());
  json buffers = json::array({json{{"uri", binName}, {"byteLength", bin.size()}}});
  if (compressed) {
    buffers.push_back(json{{"byteLength", vertexBytes + indexBytes},
                           {"extensions", {{"EXT_meshopt_compression", {{"fallback", true}}}}}});
  }
  json document{
      {"asset", {{"version", "2.0"}}},
      {"scene", 0},
      {"scenes", json::array({json{{"nodes", json::array({0})}}})},
      {"nodes", json::array({json{{"name", "Quad"}, {"mesh", 0}}})},
      {"meshes", json::array({json{{"name", "QuadMesh"},
                                   {"primitives", json::array({json{{"attributes", {{"POSITION", 0}, {"NORMAL", 1}}},
                                                                    {"indices", 2}}})}}})},
      {"buffers", buffers},
      {"bufferViews", views},
      {"accessors",
       json::array(
           {json{{"bufferView", 0},
                 {"componentType", 5126},
                 {"count", vertexCount},
                 {"type", "VEC3"},
                 {"min", json::array({-1.0, 0.0, -1.0})},
                 {"max", json::array({1.0, 2.5, 1.0})}},
            json{{"bufferView", 0},
                 {"byteOffset", 12},
                 {"componentType", 5126},
                 {"count", vertexCount},
                 {"type", "VEC3"}},
            json{{"bufferView", 1}, {"componentType", 5123}, {"count", indices16.size()}, {"type", "SCALAR"}}})},
  };
  if (compressed) {
    document["extensionsUsed"] = {"EXT_meshopt_compression"};
    document["extensionsRequired"] = {"EXT_meshopt_compression"};
  }
  REQUIRE(core::writeFile(gltf, document.dump(2)).has_value());
}

} // namespace

TEST_CASE("a glTF file's EXT_meshopt_compression geometry imports as its uncompressed source", "[assets][gltf]") {
  const std::filesystem::path directory = test::freshDirectory("sonnet_assets_gltf_meshopt");
  writeQuadGltf(directory / "plain.gltf", IndexCompression::None);
  const auto plain = importGltf(directory / "plain.gltf");
  REQUIRE(plain.has_value());
  REQUIRE(plain->meshes.size() == 1);
  const renderer::MeshData &expected = plain->meshes[0].data;
  REQUIRE(expected.vertices.size() == 4);
  REQUIRE(expected.vertices[3].position == glm::vec3{1.0f, 2.5f, 1.0f});

  // A filtered vertex buffer, with an index buffer in each of the extension's index modes.
  for (const IndexCompression mode : {IndexCompression::Triangles, IndexCompression::Indices}) {
    CAPTURE(static_cast<int>(mode));
    writeQuadGltf(directory / "packed.gltf", mode);
    const auto packed = importGltf(directory / "packed.gltf");
    REQUIRE(packed.has_value());
    REQUIRE(packed->meshes.size() == 1);
    const renderer::MeshData &mesh = packed->meshes[0].data;
    REQUIRE(mesh.indices == expected.indices);
    REQUIRE(mesh.vertices.size() == expected.vertices.size());
    for (std::size_t v = 0; v < mesh.vertices.size(); ++v) {
      REQUIRE(mesh.vertices[v].position == expected.vertices[v].position);
      REQUIRE(mesh.vertices[v].normal == expected.vertices[v].normal);
    }
  }

  // The fallback buffer has no file: not a dependency, and the structure-only read takes the file.
  const auto dependencies = gltfDependencies(directory / "packed.gltf");
  REQUIRE(dependencies.has_value());
  REQUIRE(*dependencies == std::vector<std::filesystem::path>{"packed.bin"});
  const auto structure = importGltfStructure(directory / "packed.gltf");
  REQUIRE(structure.has_value());
  REQUIRE(structure->model.nodes.size() == 1);

  // Compressed data that does not decode is an error naming the view.
  const auto bin = core::readFile(directory / "packed.bin");
  REQUIRE(bin.has_value());
  REQUIRE(core::writeFile(directory / "packed.bin", std::vector<std::byte>(bin->size(), std::byte{0xff})).has_value());
  const auto corrupt = importGltf(directory / "packed.gltf");
  REQUIRE(!corrupt.has_value());
  REQUIRE(corrupt.error().message.find("buffer view 0: EXT_meshopt_compression data does not decode") !=
          std::string::npos);
  std::filesystem::remove_all(directory);
}
