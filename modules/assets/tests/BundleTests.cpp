#include "AssetTestSupport.h"
#include "BinaryIo.h"

#include <sonnet/assets/Bundle.h>
#include <sonnet/assets/Cook.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

using namespace sonnet;
using namespace sonnet::assets;
using Catch::Approx;

namespace {

[[nodiscard]] std::vector<std::byte> bytesOf(std::string_view text) {
  const auto *data = reinterpret_cast<const std::byte *>(text.data());
  return {data, data + text.size()};
}

[[nodiscard]] renderer::MeshData triangleMesh() {
  renderer::MeshData mesh;
  mesh.vertices = {{.position = {0.0f, 0.0f, 0.0f}, .normal = {0.0f, 1.0f, 0.0f}, .uv = {0.0f, 0.0f}},
                   {.position = {1.0f, 0.0f, 0.0f}, .normal = {0.0f, 1.0f, 0.0f}, .uv = {1.0f, 0.0f}},
                   {.position = {0.0f, 0.0f, 1.0f}, .normal = {0.0f, 1.0f, 0.0f}, .uv = {0.0f, 1.0f}}};
  mesh.indices = {0, 1, 2};
  mesh.submeshes = {{.firstIndex = 0, .indexCount = 3, .materialSlot = 1}};
  mesh.skin = {{.joints = {1u, 0u, 0u, 0u}, .weights = {1.0f, 0.0f, 0.0f, 0.0f}},
               {.joints = {2u, 0u, 0u, 0u}, .weights = {1.0f, 0.0f, 0.0f, 0.0f}},
               {.joints = {3u, 0u, 0u, 0u}, .weights = {1.0f, 0.0f, 0.0f, 0.0f}}};
  return mesh;
}

} // namespace

TEST_CASE("a mesh payload round-trips through the cooked form", "[assets][bundle]") {
  const renderer::MeshData source = triangleMesh();
  const auto decoded = decodeMesh(encodeMesh(source));
  REQUIRE(decoded.has_value());
  REQUIRE(decoded->indices == source.indices);
  REQUIRE(decoded->vertices.size() == source.vertices.size());
  REQUIRE(decoded->vertices[2].position == source.vertices[2].position);
  REQUIRE(decoded->vertices[1].uv == source.vertices[1].uv);
  REQUIRE(decoded->submeshes.size() == 1);
  REQUIRE(decoded->submeshes[0].materialSlot == 1);
  REQUIRE(decoded->skin.size() == 3);
  REQUIRE(decoded->skin[2].joints.x == 3u);
}

TEST_CASE("a texture payload round-trips with its description", "[assets][bundle]") {
  renderer::TextureData source;
  source.size = {2, 2};
  source.format = rhi::Format::R16G16B16A16Sfloat;
  source.data.assign(static_cast<std::size_t>(source.expectedSize()), std::byte{0x7f});
  const auto decoded = decodeTexture(encodeTexture(source));
  REQUIRE(decoded.has_value());
  REQUIRE(decoded->size == source.size);
  REQUIRE(decoded->format == source.format);
  REQUIRE(decoded->mipLevels == 1);
  REQUIRE(!decoded->cube);
  REQUIRE(decoded->data == source.data);
}

TEST_CASE("skins, clips and models round-trip through the cooked form", "[assets][bundle]") {
  Skin skin;
  skin.joints = {"Rig/Root", "Rig/Root/Tip"};
  skin.inverseBindMatrices = {glm::mat4{1.0f}, glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, -1.0f, 0.0f})};
  const auto decodedSkin = decodeSkin(encodeSkin(skin));
  REQUIRE(decodedSkin.has_value());
  REQUIRE(decodedSkin->joints == skin.joints);
  REQUIRE(decodedSkin->inverseBindMatrices[1][3][1] == Approx(-1.0f));

  AnimationClip clip;
  clip.duration = 1.5f;
  clip.channels = {{.target = "Rig/Root/Tip",
                    .path = AnimationPath::Rotation,
                    .interpolation = Interpolation::Step,
                    .times = {0.0f, 1.5f},
                    .values = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 0.0f}}}};
  const auto decodedClip = decodeAnimation(encodeAnimation(clip));
  REQUIRE(decodedClip.has_value());
  REQUIRE(decodedClip->duration == Approx(1.5f));
  REQUIRE(decodedClip->channels.size() == 1);
  REQUIRE(decodedClip->channels[0].target == "Rig/Root/Tip");
  REQUIRE(decodedClip->channels[0].path == AnimationPath::Rotation);
  REQUIRE(decodedClip->channels[0].interpolation == Interpolation::Step);
  REQUIRE(decodedClip->channels[0].times == clip.channels[0].times);
  REQUIRE(decodedClip->channels[0].values[1].z == Approx(1.0f));

  Model model;
  const core::Uuid mesh = core::Uuid::generate();
  const core::Uuid animation = core::Uuid::generate();
  model.nodes = {{.name = "Crate", .parent = -1, .position = {1.0f, 2.0f, 3.0f}, .mesh = mesh},
                 {.name = "Lid", .parent = 0, .scale = {2.0f, 2.0f, 2.0f}}};
  model.animations = {animation};
  const auto decodedModel = decodeModel(encodeModel(model));
  REQUIRE(decodedModel.has_value());
  REQUIRE(decodedModel->nodes.size() == 2);
  REQUIRE(decodedModel->nodes[0].name == "Crate");
  REQUIRE(decodedModel->nodes[0].mesh == mesh);
  REQUIRE(decodedModel->nodes[0].position == glm::vec3{1.0f, 2.0f, 3.0f});
  REQUIRE(decodedModel->nodes[1].parent == 0);
  REQUIRE(decodedModel->nodes[1].scale == glm::vec3{2.0f, 2.0f, 2.0f});
  REQUIRE(decodedModel->animations == std::vector{animation});
}

TEST_CASE("a truncated or foreign payload is an error, never a read past the end", "[assets][bundle]") {
  const std::vector<std::byte> mesh = encodeMesh(triangleMesh());
  for (const std::size_t length : {std::size_t{0}, std::size_t{3}, std::size_t{8}, mesh.size() / 2, mesh.size() - 1}) {
    REQUIRE(!decodeMesh(std::span{mesh}.first(length)).has_value());
  }
  // The right length, the wrong kind: the tag catches it before anything is decoded.
  REQUIRE(!decodeSkin(mesh).has_value());
  REQUIRE(!decodeTexture(mesh).has_value());
  REQUIRE(!decodeAnimation(mesh).has_value());
  REQUIRE(!decodeModel(mesh).has_value());
  REQUIRE(!decodeJson(mesh).has_value());

  // A count that promises more than the payload holds allocates nothing.
  std::vector<std::byte> lying = encodeSkin({.joints = {"a"}, .inverseBindMatrices = {glm::mat4{1.0f}}});
  lying[4] = std::byte{0xff};
  lying[5] = std::byte{0xff};
  REQUIRE(!decodeSkin(lying).has_value());
}

// An empty vector's data() is null, and the sanitizer job failed on the memcpy that took it,
// even with nothing to copy (the "Box" mesh of a cooked bundle has no skin).
TEST_CASE("an empty array and an empty string read back without touching memcpy", "[assets][bundle]") {
  detail::ByteWriter writer;
  writer.array(std::span<const float>{});
  writer.string({});
  writer.u32(7);
  const std::vector<std::byte> bytes = writer.take();

  detail::ByteReader reader{bytes};
  std::vector<float> values;
  reader.array(values);
  const std::string text = reader.string();
  REQUIRE(reader.u32() == 7);
  REQUIRE(reader.ok());
  REQUIRE(values.empty());
  REQUIRE(text.empty());
  REQUIRE(reader.remaining() == 0);
}

TEST_CASE("a bundle round-trips its manifest, assets and files", "[assets][bundle]") {
  const std::filesystem::path directory = test::freshDirectory("sonnet_assets_bundle");
  const std::filesystem::path file = directory / "game.sbundle";

  const core::Uuid meshId = core::Uuid::generate();
  const core::Uuid modelId = core::Uuid::generate();
  const core::Uuid materialId = core::Uuid::generate();
  {
    auto writer = BundleWriter::create(file, {.name = "Basic",
                                              .engineVersion = {}, // the writer stamps the running version
                                              .platform = CookPlatform::Windows,
                                              .startScene = "scenes/main.scene.json"});
    REQUIRE(writer.has_value());
    REQUIRE(writer
                ->addAsset({.uuid = meshId,
                            .type = AssetType::Mesh,
                            .name = "Crate",
                            .parent = modelId,
                            .materials = {materialId, {}}}, // a slot with no material stays empty
                           encodeMesh(triangleMesh()))
                .has_value());
    REQUIRE(writer
                ->addAsset({.uuid = modelId, .type = AssetType::Model, .name = "crate", .parent = {}, .materials = {}},
                           encodeModel({}))
                .has_value());
    // A script's payload is its source, stored as it is.
    REQUIRE(writer
                ->addAsset({.uuid = core::Uuid::generate(),
                            .type = AssetType::Script,
                            .name = "spin",
                            .parent = {},
                            .materials = {}},
                           bytesOf("return {}\n"))
                .has_value());
    REQUIRE(writer->addFile("scenes/main.scene.json", encodeJson({{"version", 2}})).has_value());
    REQUIRE(writer->addFile("prefabs/crate.prefab.json", encodeJson({{"version", 2}})).has_value());
    REQUIRE(writer->finish().has_value());
  }

  { // the bundle keeps its file open, which Windows will not let remove_all delete
    const auto bundle = Bundle::open(file);
    REQUIRE(bundle.has_value());
    REQUIRE(bundle->manifest().name == "Basic");
    REQUIRE(bundle->manifest().platform == CookPlatform::Windows);
    REQUIRE(bundle->manifest().startScene == "scenes/main.scene.json");
    REQUIRE(!bundle->manifest().engineVersion.empty()); // stamped by the writer
    REQUIRE(bundle->assets().size() == 3);
    REQUIRE(bundle->files() == std::vector<std::string>{"prefabs/crate.prefab.json", "scenes/main.scene.json"});
    REQUIRE(bundle->contains(meshId));
    REQUIRE(bundle->contains("scenes/main.scene.json"));
    REQUIRE(!bundle->contains(core::Uuid::generate()));
    REQUIRE(!bundle->contains("scenes/nowhere.scene.json"));

    const std::span<const BundleAsset> entries = bundle->assets();
    const auto cooked = std::ranges::find_if(entries, [&](const BundleAsset &asset) { return asset.uuid == meshId; });
    REQUIRE(cooked != entries.end());
    REQUIRE(cooked->type == AssetType::Mesh);
    REQUIRE(cooked->name == "Crate");
    REQUIRE(cooked->parent == modelId);
    REQUIRE(cooked->materials == std::vector<core::Uuid>{materialId, {}});

    const auto payload = bundle->read(meshId);
    REQUIRE(payload.has_value());
    const auto mesh = decodeMesh(*payload);
    REQUIRE(mesh.has_value());
    REQUIRE(mesh->indices == triangleMesh().indices);

    const auto scene = bundle->read("scenes/main.scene.json");
    REQUIRE(scene.has_value());
    const auto document = decodeJson(*scene);
    REQUIRE(document.has_value());
    REQUIRE(document->at("version") == 2);

    REQUIRE(!bundle->read(core::Uuid::generate()).has_value());
    REQUIRE(!bundle->read("scenes/nowhere.scene.json").has_value());
  }
  std::filesystem::remove_all(directory);
}

TEST_CASE("a file that is not a bundle is refused", "[assets][bundle]") {
  const std::filesystem::path directory = test::freshDirectory("sonnet_assets_bundle_bad");
  REQUIRE(!Bundle::open(directory / "nothing.sbundle").has_value());

  const std::filesystem::path wrong = directory / "wrong.sbundle";
  REQUIRE(
      core::writeFile(wrong, std::string_view{"not a bundle at all, but long enough to hold a header"}).has_value());
  REQUIRE(!Bundle::open(wrong).has_value());

  // A bundle whose version is not this one: readable header, refused anyway.
  const std::filesystem::path future = directory / "future.sbundle";
  {
    auto writer =
        BundleWriter::create(future, {.name = "Future", .engineVersion = {}, .platform = {}, .startScene = {}});
    REQUIRE(writer.has_value());
    REQUIRE(writer->finish().has_value());
  }
  auto bytes = core::readFile(future);
  REQUIRE(bytes.has_value());
  (*bytes)[8] = std::byte{BundleVersion + 1};
  REQUIRE(core::writeFile(future, *bytes).has_value());
  REQUIRE(!Bundle::open(future).has_value());
  std::filesystem::remove_all(directory);
}
