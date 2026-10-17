#include "AssetTestSupport.h"

#include <sonnet/assets/AssetDatabase.h>
#include <sonnet/assets/Thumbnail.h>
#include <sonnet/core/JobSystem.h>
#include <sonnet/platform/Platform.h>
#include <sonnet/renderer/Renderer.h>
#include <sonnet/rhi/NullDevice.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>

using namespace sonnet;
using namespace sonnet::assets;

namespace {

struct Fixture {
  platform::Platform platform{{.headless = true}};
  std::unique_ptr<rhi::NullDevice> device = rhi::createNullDevice();
  renderer::Renderer renderer{*device, platform.basePath() / "shaders"};
  core::JobSystem jobs{{.workerCount = 0}};
  std::filesystem::path root = test::freshDirectory("sonnet_assets_thumbnails");
  std::vector<std::string> roots{"assets"};
  AssetDatabase database{renderer, jobs};

  Fixture() {
    std::filesystem::create_directories(root / "assets");
    // 300x200, red on the left half and blue on the right.
    std::vector<std::uint8_t> pixels;
    for (int y = 0; y < 200; ++y) {
      for (int x = 0; x < 300; ++x) {
        pixels.insert(pixels.end(), x < 150 ? std::initializer_list<std::uint8_t>{255, 0, 0, 255}
                                            : std::initializer_list<std::uint8_t>{0, 0, 255, 255});
      }
    }
    REQUIRE(core::writeFile(root / "assets" / "wide.png", test::encodePng({300, 200}, pixels)).has_value());
    REQUIRE(core::writeFile(root / "assets" / "tiny.png", test::encodePng({2, 2}, test::quadPixels())).has_value());
    REQUIRE(core::writeFile(root / "assets" / "sky.hdr",
                            test::encodeHdr({512, 256}, std::vector<float>(std::size_t{512} * 256 * 3, 1.0f)))
                .has_value());
    test::writeBoxGltf(root / "assets" / "crate.gltf", "tiny.png");
    database.open(root, roots);
  }
  ~Fixture() {
    std::filesystem::remove_all(root);
  }

  [[nodiscard]] core::Uuid uuidOf(const char *file) const {
    const AssetInfo *info = database.findByPath(root / "assets" / file);
    REQUIRE(info != nullptr);
    return info->uuid;
  }
};

} // namespace

TEST_CASE("a texture's thumbnail is the largest mip level that fits, cached beside the other cooked files",
          "[assets][thumbnail]") {
  Fixture fixture;
  const core::Uuid wide = fixture.uuidOf("wide.png");
  const auto request = fixture.database.thumbnailRequest(wide);
  REQUIRE(request.has_value());
  const auto thumbnail = makeThumbnail(*request);
  REQUIRE(thumbnail.has_value());
  CHECK(thumbnail->size == glm::uvec2{75, 50});
  CHECK(thumbnail->format == rhi::Format::R8G8B8A8Unorm);
  CHECK(thumbnail->mipLevels == 1);
  REQUIRE(thumbnail->data.size() == std::size_t{75} * 50 * 4);
  CHECK(std::to_integer<int>(thumbnail->data[0]) == 255);            // left: red
  CHECK(std::to_integer<int>(thumbnail->data[(74 * 4) + 2]) == 255); // right: blue
  CHECK(std::filesystem::exists(thumbnailFile(fixture.database.cacheDirectory(), wide)));

  // A smaller image than the thumbnail is kept as it is.
  const auto tiny = makeThumbnail(*fixture.database.thumbnailRequest(fixture.uuidOf("tiny.png")));
  REQUIRE(tiny.has_value());
  CHECK(tiny->size == glm::uvec2{2, 2});
}

TEST_CASE("a thumbnail comes back from the cache until the source or the sidecar is newer", "[assets][thumbnail]") {
  Fixture fixture;
  const core::Uuid wide = fixture.uuidOf("wide.png");
  const auto source = fixture.root / "assets" / "wide.png";
  const auto request = *fixture.database.thumbnailRequest(wide);
  REQUIRE(makeThumbnail(request).has_value());

  // The cache entry answers while the source is gone.
  const auto bytes = core::readFile(source);
  REQUIRE(bytes.has_value());
  std::filesystem::remove(source);
  const auto cached = makeThumbnail(request);
  REQUIRE(cached.has_value());
  CHECK(cached->size == glm::uvec2{75, 50});

  // A source written after the entry makes it stale, and the thumbnail is made again from the new one.
  const auto entry = thumbnailFile(fixture.database.cacheDirectory(), wide);
  const auto entryTime = std::filesystem::last_write_time(entry);
  const auto newer = test::encodePng({64, 32}, std::vector<std::uint8_t>(std::size_t{64} * 32 * 4, 200));
  for (int attempt = 0; attempt < 50; ++attempt) {
    REQUIRE(core::writeFile(source, newer).has_value());
    if (std::filesystem::last_write_time(source) > entryTime) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
  }
  auto stale = request;
  stale.sourceTime = std::filesystem::last_write_time(source);
  const auto remade = makeThumbnail(stale);
  REQUIRE(remade.has_value());
  CHECK(remade->size == glm::uvec2{64, 32});
  CHECK((std::filesystem::last_write_time(entry) >=
         stale.sourceTime)); // not decomposed: Catch cannot print a file time on macOS
}

TEST_CASE("an environment's thumbnail is its map downscaled and tone mapped", "[assets][thumbnail]") {
  Fixture fixture;
  const auto thumbnail = makeThumbnail(*fixture.database.thumbnailRequest(fixture.uuidOf("sky.hdr")));
  REQUIRE(thumbnail.has_value());
  CHECK(thumbnail->size == glm::uvec2{ThumbnailSize, 64});
  // 1 / (1 + 1) = 0.5 linear, 188 in sRGB; opaque.
  CHECK(std::to_integer<int>(thumbnail->data[0]) == 188);
  CHECK(std::to_integer<int>(thumbnail->data[3]) == 255);
}

TEST_CASE("textures, environments, materials, models and meshes have a thumbnail request", "[assets][thumbnail]") {
  Fixture fixture;
  const AssetInfo *crate = fixture.database.findByPath(fixture.root / "assets" / "crate.gltf");
  REQUIRE(crate != nullptr);
  // A model is rendered: it is as old as its file and its sidecar, and cannot be made from them.
  const auto model = fixture.database.thumbnailRequest(crate->uuid);
  REQUIRE(model.has_value());
  CHECK(model->type == AssetType::Model);
  CHECK(model->source == crate->source);
  CHECK(model->sidecar.filename() == "crate.gltf.meta");
  CHECK(isRendered(*model));
  CHECK_FALSE(makeThumbnail(*model).has_value());
  bool subAssets = false;
  for (const AssetInfo *info : fixture.database.assets()) {
    if (info->parent != crate->uuid) {
      continue;
    }
    subAssets = true;
    const auto request = fixture.database.thumbnailRequest(info->uuid);
    if (info->type == AssetType::Mesh) {
      // A glTF mesh is as old as the file it is in.
      REQUIRE(request.has_value());
      CHECK(request->source == crate->source);
      CHECK(request->sidecar == model->sidecar);
      CHECK(request->store);
      CHECK(isRendered(*request));
    } else {
      CHECK_FALSE(request.has_value());
    }
  }
  CHECK(subAssets);
  CHECK_FALSE(fixture.database.thumbnailRequest(core::Uuid{}).has_value());
  // A built-in mesh has no file, so nothing it could be as old as and nowhere to keep its picture.
  const auto box = fixture.database.thumbnailRequest(builtin::box());
  REQUIRE(box.has_value());
  CHECK(box->type == AssetType::Mesh);
  CHECK_FALSE(box->store);
  CHECK_FALSE(cachedThumbnail(*box).has_value());
  CHECK_FALSE(isRendered(*fixture.database.thumbnailRequest(fixture.uuidOf("wide.png"))));
}

TEST_CASE("unregistering an asset drops its thumbnail", "[assets][thumbnail]") {
  Fixture fixture;
  const core::Uuid wide = fixture.uuidOf("wide.png");
  REQUIRE(makeThumbnail(*fixture.database.thumbnailRequest(wide)).has_value());
  const auto entry = thumbnailFile(fixture.database.cacheDirectory(), wide);
  REQUIRE(std::filesystem::exists(entry));
  fixture.database.unregister(wide);
  CHECK_FALSE(std::filesystem::exists(entry));
}

TEST_CASE("a material's thumbnail is rendered, and is as old as the material and the textures it reads",
          "[assets][thumbnail][material]") {
  Fixture fixture;
  const core::Uuid wide = fixture.uuidOf("wide.png");
  MaterialSource source;
  source.baseColorTexture = wide;
  const auto created = fixture.database.createMaterial(fixture.root / "assets" / "paint.material.json", source);
  REQUIRE(created.has_value());
  const core::Uuid paint = *created;
  REQUIRE(fixture.database.saveMaterial(paint).has_value());
  static_cast<void>(fixture.database.takeChangedMaterials());

  auto request = fixture.database.thumbnailRequest(paint);
  REQUIRE(request.has_value());
  CHECK(request->type == AssetType::Material);
  CHECK(request->store);
  // The texture's file and its sidecar.
  REQUIRE(request->dependencies.size() == 2);
  CHECK(request->dependencies[0].filename() == "wide.png");
  // There is no file to make it from: the editor renders it.
  CHECK_FALSE(makeThumbnail(*request).has_value());
  CHECK_FALSE(cachedThumbnail(*request).has_value());

  const renderer::TextureData picture = renderer::solidTexture({10, 20, 30, 255});
  REQUIRE(storeThumbnail(*request, picture).has_value());
  const auto entry = thumbnailFile(fixture.database.cacheDirectory(), paint);
  REQUIRE(std::filesystem::exists(entry));
  const auto cached = cachedThumbnail(*request);
  REQUIRE(cached.has_value());
  CHECK(cached->size == picture.size);
  CHECK(cached->data == picture.data);

  // A texture it reads written after the entry makes the entry stale.
  std::filesystem::last_write_time(request->dependencies[0],
                                   std::filesystem::file_time_type::clock::now() + std::chrono::hours{1});
  CHECK_FALSE(cachedThumbnail(*request).has_value());

  // An edit that is not saved is reported, and its picture is not stored, as the file does not match it.
  source.baseColor = {0.2f, 0.4f, 0.8f, 1.0f};
  fixture.database.setMaterialSource(paint, source);
  CHECK(fixture.database.takeChangedMaterials() == std::vector<core::Uuid>{paint});
  CHECK(fixture.database.takeChangedMaterials().empty());
  request = fixture.database.thumbnailRequest(paint);
  REQUIRE(request.has_value());
  CHECK_FALSE(request->store);
  std::filesystem::remove(entry);
  REQUIRE(storeThumbnail(*request, picture).has_value());
  CHECK_FALSE(std::filesystem::exists(entry));
  // Saving it is a change too, and the picture can be kept from then on.
  REQUIRE(fixture.database.saveMaterial(paint).has_value());
  CHECK(fixture.database.takeChangedMaterials() == std::vector<core::Uuid>{paint});
  CHECK(fixture.database.thumbnailRequest(paint)->store);

  REQUIRE(storeThumbnail(*fixture.database.thumbnailRequest(paint), picture).has_value());
  REQUIRE(std::filesystem::exists(entry));
  fixture.database.unregister(paint);
  CHECK_FALSE(std::filesystem::exists(entry));
}
