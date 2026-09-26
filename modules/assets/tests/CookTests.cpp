#include "AssetTestSupport.h"

#include <sonnet/assets/AssetDatabase.h>
#include <sonnet/assets/Cook.h>
#include <sonnet/core/JobSystem.h>

#include <sonnet/platform/Platform.h>
#include <sonnet/renderer/Renderer.h>
#include <sonnet/rhi/NullDevice.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <set>
#include <tuple>
#include <vector>

using namespace sonnet;
using namespace sonnet::assets;
using Catch::Approx;
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
  REQUIRE(source.vertices.size() == 8uz * 8 * 6); // three per triangle, nothing shared

  MeshCookStatistics statistics;
  const MeshData cooked = cookMesh(source, &statistics);

  // A grid of 8x8 quads has 9x9 distinct corners; welding finds exactly those.
  REQUIRE(statistics.verticesBefore == source.vertices.size());
  REQUIRE(cooked.vertices.size() == 9uz * 9);
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

// Cooking a whole project needs a renderer for the importers to upload into; the null device is
// what the cook tool uses too (ADR-0011).
namespace {

struct ProjectFixture {
  platform::Platform platform{{.headless = true}};
  std::unique_ptr<rhi::NullDevice> device = rhi::createNullDevice();
  renderer::Renderer renderer{*device, platform.basePath() / "shaders"};
  core::JobSystem jobs{{.workerCount = 2}};
  std::filesystem::path root = test::freshDirectory("sonnet_assets_cook");
  Project project;

  ProjectFixture() {
    std::filesystem::create_directories(root / "assets" / "models");
    std::filesystem::create_directories(root / "scripts");
    std::filesystem::create_directories(root / "scenes");
    REQUIRE(core::writeFile(root / "assets" / "models" / "wood.png", test::encodePng({2, 2}, test::quadPixels()))
                .has_value());
    test::writeBoxGltf(root / "assets" / "models" / "crate.gltf", "wood.png");
    test::writeSkinnedGltf(root / "assets" / "models" / "reed.gltf");
    REQUIRE(core::writeFile(root / "assets" / "sky.hdr", test::encodeHdr({4, 2}, std::vector<float>(4uz * 2 * 3, 0.5f)))
                .has_value());
    MaterialSource painted;
    painted.baseColor = {0.2f, 0.4f, 0.6f, 1.0f};
    painted.roughness = 0.25f;
    REQUIRE(core::writeFile(root / "assets" / "painted.material.json", saveMaterial(painted).dump(2)).has_value());
    REQUIRE(core::writeFile(root / "scripts" / "spin.lua", std::string_view{"return { update = function() end }\n"})
                .has_value());
    REQUIRE(core::writeFile(root / "scenes" / "main.scene.json", std::string_view{R"({"version": 2, "entities": []})"})
                .has_value());
    REQUIRE(
        core::writeFile(root / "scenes" / "crate.prefab.json", std::string_view{R"({"version": 2, "entities": []})"})
            .has_value());
    project.root = root;
    project.name = "Cooked";
    project.assetRoots = {"assets", "scripts"};
    REQUIRE(project.save().has_value());
  }
  ~ProjectFixture() {
    std::filesystem::remove_all(root);
  }
};

[[nodiscard]] const AssetInfo *byName(const AssetDatabase &database, std::string_view name, AssetType type) {
  for (const AssetInfo *info : database.assets(type)) {
    if (info->name == name) {
      return info;
    }
  }
  return nullptr;
}

// The identity of an asset the fixture put there: a missing one is the test's own bug, so it
// fails here rather than dereferencing null further down.
[[nodiscard]] core::Uuid idOf(const AssetDatabase &database, std::string_view name, AssetType type) {
  const AssetInfo *info = byName(database, name, type);
  REQUIRE(info != nullptr);
  return info->uuid;
}

} // namespace

TEST_CASE("a project cooks into a bundle the database opens again", "[assets][cook]") {
  ProjectFixture fixture;
  const std::filesystem::path out = fixture.root / "export";

  core::Uuid meshId;
  core::Uuid materialId;
  core::Uuid skinId;
  core::Uuid clipId;
  core::Uuid scriptId;
  core::Uuid environmentId;
  std::size_t sourceAssetCount = 0;
  {
    AssetDatabase database{fixture.renderer, fixture.jobs};
    database.open(fixture.root, fixture.project.assetRoots);
    meshId = idOf(database, "CrateMesh", AssetType::Mesh);
    materialId = idOf(database, "painted", AssetType::Material);
    skinId = idOf(database, "StripSkin", AssetType::Skin);
    clipId = idOf(database, "Bend", AssetType::Animation);
    scriptId = idOf(database, "spin", AssetType::Script);
    environmentId = idOf(database, "sky", AssetType::Environment);
    sourceAssetCount = database.assets().size();

    const auto report = cook(database, fixture.project, {.outputDirectory = out, .platform = CookPlatform::Windows});
    REQUIRE(report.has_value());
    REQUIRE(report->warnings.empty());
    REQUIRE(report->bundle == out / "game.sbundle");
    REQUIRE(report->fileCount == 2); // the scene and the prefab
    // Every asset but the five built-in primitives, which the player registers for itself.
    REQUIRE(report->assetCount == sourceAssetCount - 5);
    REQUIRE(report->meshes.verticesBefore > 0);
    // bytesWritten counts the payloads; the index and header follow them in the file.
    REQUIRE(std::filesystem::file_size(report->bundle) > report->bytes);

    // Cooking a project the database does not have open is refused rather than half done.
    Project elsewhere = fixture.project;
    elsewhere.root = fixture.root / "not-here";
    REQUIRE(!cook(database, elsewhere, {.outputDirectory = out, .platform = CookPlatform::Linux}).has_value());
  }

  AssetDatabase player{fixture.renderer, fixture.jobs};
  REQUIRE(player.openBundle(out / "game.sbundle").has_value());
  REQUIRE(player.isOpen());
  const Bundle *bundle = player.bundle();
  REQUIRE(bundle != nullptr);
  if (bundle == nullptr) {
    return; // REQUIRE has already failed; GCC 14 cannot tell and reports the null path at -O3
  }
  REQUIRE(bundle->manifest().name == "Cooked");
  REQUIRE(bundle->manifest().platform == CookPlatform::Windows);
  REQUIRE(bundle->manifest().startScene == "scenes/main.scene.json");
  REQUIRE(player.assets().size() == sourceAssetCount); // the built-ins are back too
  REQUIRE(player.pollChanges().empty());               // nothing to watch in a bundle

  // The primitives still resolve, from the renderer rather than from the bundle.
  REQUIRE(player.mesh(builtin::box()));
  // A cooked mesh comes back with its data, which physics builds colliders from.
  REQUIRE(player.mesh(meshId));
  const renderer::MeshData *data = player.meshData(meshId);
  REQUIRE(data != nullptr);
  REQUIRE(data->indices.size() == 36); // the box's twelve triangles
  REQUIRE(!data->vertices.empty());

  const MaterialSource *material = player.materialSource(materialId);
  REQUIRE(material != nullptr);
  REQUIRE(material->baseColor == glm::vec4{0.2f, 0.4f, 0.6f, 1.0f});
  REQUIRE(material->roughness == Approx(0.25f));

  const Skin *skin = player.skin(skinId);
  REQUIRE(skin != nullptr);
  REQUIRE(skin->joints.size() == 2);
  REQUIRE(skin->revision > 0);

  const AnimationClip *clip = player.animation(clipId);
  REQUIRE(clip != nullptr);
  REQUIRE(clip->duration > 0.0f);
  REQUIRE(!clip->channels.empty());

  const ScriptSource *script = player.script(scriptId);
  REQUIRE(script != nullptr);
  REQUIRE(script->code.starts_with("return {"));

  REQUIRE(player.environment(environmentId));
  const AssetInfo *crate = byName(player, "crate", AssetType::Model);
  REQUIRE(crate != nullptr);
  const Model *model = player.model(crate->uuid);
  REQUIRE(model != nullptr);
  REQUIRE(!model->nodes.empty());

  // The textures cooked with the model are there, and the scene and prefab came along.
  REQUIRE(player.texture(idOf(player, "image 0", AssetType::Texture)));
  const auto scene = player.bundle()->read("scenes/main.scene.json");
  REQUIRE(scene.has_value());
  const auto document = decodeJson(*scene);
  REQUIRE(document.has_value());
  REQUIRE(document->at("version") == 2);
}

TEST_CASE("cooking the playground alone keeps prefabs and assets", "[assets][cook]") {
  platform::Platform platform{{.headless = true}};
  std::filesystem::path sample;
  for (std::filesystem::path base = std::filesystem::absolute(platform.basePath()); !base.empty();
       base = base.parent_path()) {
    const auto candidate = base / "apps" / "samples" / "basic";
    if (std::filesystem::is_regular_file(candidate / "project.json")) {
      sample = candidate;
      break;
    }
    if (base == base.root_path()) {
      break;
    }
  }
  REQUIRE_FALSE(sample.empty());
  const auto project = Project::open(sample);
  REQUIRE(project.has_value());
  const auto device = rhi::createNullDevice();
  renderer::Renderer renderer{*device, platform.basePath() / "shaders"};
  core::JobSystem jobs{{.workerCount = 0}};
  AssetDatabase database{renderer, jobs};
  database.open(project->root, project->assetRoots);
  const auto out = test::freshDirectory("sonnet_playground_cook");
  const auto report = cook(database, *project, {.outputDirectory = out, .scene = "scenes/playground.scene.json"});
  REQUIRE(report.has_value());
  REQUIRE(report->assetCount == database.assets().size() - 5); // built-in primitives are supplied by the player
  REQUIRE(report->fileCount == 1 + project->files(".prefab.json").size());
  {
    const auto bundle = Bundle::open(report->bundle);
    REQUIRE(bundle.has_value());
    REQUIRE(bundle->manifest().startScene == "scenes/playground.scene.json");
    REQUIRE(bundle->read("scenes/playground.scene.json").has_value());
    REQUIRE_FALSE(bundle->read("scenes/main.scene.json").has_value());
    REQUIRE(bundle->assets().size() == report->assetCount);
    for (const auto &prefab : project->files(".prefab.json")) {
      REQUIRE(bundle->read(project->relative(prefab)).has_value());
    }
  }
  REQUIRE_FALSE(cook(database, *project, {.outputDirectory = out, .scene = "scenes/missing.scene.json"}).has_value());
  REQUIRE(std::filesystem::remove_all(out) > 0);
}
