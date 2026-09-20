#include "AssetTestSupport.h"

#include <sonnet/assets/AssetDatabase.h>

#include <sonnet/platform/Platform.h>
#include <sonnet/renderer/Renderer.h>
#include <sonnet/rhi/NullDevice.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>

using namespace sonnet;
using namespace sonnet::assets;
using Catch::Approx;

namespace {

struct Fixture {
  platform::Platform platform{{.headless = true}};
  std::unique_ptr<rhi::NullDevice> device = rhi::createNullDevice();
  renderer::Renderer renderer{*device, platform.basePath() / "shaders"};
  std::filesystem::path root = test::freshDirectory("sonnet_assets_db");
  std::vector<std::string> roots{"assets"};

  Fixture() {
    std::filesystem::create_directories(root / "assets" / "models");
    REQUIRE(core::writeFile(root / "assets" / "wood.png", test::encodePng({2, 2}, test::quadPixels())).has_value());
    REQUIRE(core::writeFile(root / "assets" / "models" / "wood.png", test::encodePng({2, 2}, test::quadPixels()))
                .has_value());
    test::writeBoxGltf(root / "assets" / "models" / "crate.gltf", "wood.png");
    REQUIRE(core::writeFile(root / "assets" / "sky.hdr", test::encodeHdr({4, 2}, std::vector<float>(4 * 2 * 3, 0.5f)))
                .has_value());
    MaterialSource painted;
    painted.baseColor = {0.2f, 0.4f, 0.6f, 1.0f};
    painted.roughness = 0.3f;
    REQUIRE(core::writeFile(root / "assets" / "painted.material.json", saveMaterial(painted).dump(2)).has_value());
  }
  ~Fixture() {
    std::filesystem::remove_all(root);
  }

  // Modification times have a coarse resolution on some file systems; a change has to land in a
  // later tick to be noticed.
  static void touchLater(const std::filesystem::path &file, std::span<const std::byte> bytes) {
    const auto before = std::filesystem::last_write_time(file);
    for (int attempt = 0; attempt < 50; ++attempt) {
      REQUIRE(core::writeFile(file, bytes).has_value());
      if (std::filesystem::last_write_time(file) != before) {
        return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds{20});
    }
    FAIL("the file's modification time did not change");
  }
};

const AssetInfo *byName(const AssetDatabase &database, std::string_view name, AssetType type) {
  for (const AssetInfo *info : database.assets(type)) {
    if (info->name == name) {
      return info;
    }
  }
  return nullptr;
}

} // namespace

TEST_CASE("opening a project writes sidecars and keeps identities across reopens", "[assets][database]") {
  Fixture fixture;
  core::Uuid woodUuid;
  core::Uuid crateUuid;
  core::Uuid crateMeshUuid;
  {
    AssetDatabase database{fixture.renderer};
    REQUIRE(database.find(builtin::box()) != nullptr);
    REQUIRE(database.find(builtin::box())->name == "Box");
    database.open(fixture.root, fixture.roots);
    REQUIRE(std::filesystem::exists(fixture.root / "assets" / "wood.png.meta"));
    REQUIRE(std::filesystem::exists(fixture.root / "assets" / "models" / "crate.gltf.meta"));

    // Two files are called wood.png; the path tells them apart.
    const AssetInfo *wood = database.findByPath(fixture.root / "assets" / "wood.png");
    REQUIRE(wood != nullptr);
    REQUIRE(wood->name == "wood");
    REQUIRE(wood->type == AssetType::Texture);
    woodUuid = wood->uuid;
    REQUIRE(database.textureSettings(woodUuid) == TextureSettings{});
    REQUIRE(database.assets(AssetType::Texture).size() == 3); // the two files and the crate's image

    const AssetInfo *crate = byName(database, "crate", AssetType::Model);
    REQUIRE(crate != nullptr);
    crateUuid = crate->uuid;
    const AssetInfo *crateMesh = byName(database, "CrateMesh", AssetType::Mesh);
    REQUIRE(crateMesh != nullptr);
    REQUIRE(crateMesh->parent == crateUuid);
    REQUIRE(crateMesh->uuid == core::Uuid::derive(crateUuid, "mesh/0"));
    crateMeshUuid = crateMesh->uuid;
    REQUIRE(crateMesh->materials.size() == 1);
    REQUIRE(crateMesh->materials[0] == core::Uuid::derive(crateUuid, "material/0"));
    REQUIRE(byName(database, "Wood", AssetType::Material) != nullptr);
    REQUIRE(byName(database, "painted", AssetType::Material) != nullptr);
    REQUIRE(byName(database, "sky", AssetType::Environment) != nullptr);
    REQUIRE(database.assets(AssetType::Mesh).size() == 6); // five built-ins and the crate
  }
  {
    AssetDatabase database{fixture.renderer};
    database.open(fixture.root, fixture.roots);
    REQUIRE(database.findByPath(fixture.root / "assets" / "wood.png")->uuid == woodUuid);
    REQUIRE(byName(database, "crate", AssetType::Model)->uuid == crateUuid);
    REQUIRE(byName(database, "CrateMesh", AssetType::Mesh)->uuid == crateMeshUuid);
    database.close();
    REQUIRE(database.assets(AssetType::Texture).empty());
    REQUIRE(database.find(builtin::plane()) != nullptr);
  }
}

TEST_CASE("assets load on first use, into the cache for textures, and stay loaded", "[assets][database]") {
  Fixture fixture;
  AssetDatabase database{fixture.renderer};
  database.open(fixture.root, fixture.roots);

  const renderer::MeshHandle box = database.mesh(builtin::box());
  REQUIRE(fixture.renderer.isValid(box));
  REQUIRE(database.mesh(builtin::box()) == box);
  REQUIRE(!database.mesh(core::Uuid::generate()).isValid());

  const core::Uuid woodUuid = database.findByPath(fixture.root / "assets" / "wood.png")->uuid;
  const renderer::TextureHandle wood = database.texture(woodUuid);
  REQUIRE(fixture.renderer.isValid(wood));
  REQUIRE(std::filesystem::exists(database.cacheDirectory() / (woodUuid.toString() + ".ktx2")));
  REQUIRE(database.texture(woodUuid) == wood);
  REQUIRE(!database.texture({}).isValid());

  const core::Uuid paintedUuid = byName(database, "painted", AssetType::Material)->uuid;
  const renderer::MaterialHandle painted = database.material(paintedUuid);
  REQUIRE(fixture.renderer.isValid(painted));
  REQUIRE(fixture.renderer.material(painted).roughness == Approx(0.3f));
  REQUIRE(database.materialSource(paintedUuid)->baseColor.b == Approx(0.6f));

  const core::Uuid crateUuid = byName(database, "crate", AssetType::Model)->uuid;
  const Model *model = database.model(crateUuid);
  REQUIRE(model != nullptr);
  REQUIRE(model->nodes.size() == 1);
  REQUIRE(model->nodes[0].mesh == core::Uuid::derive(crateUuid, "mesh/0"));
  const renderer::MeshHandle crateMesh = database.mesh(model->nodes[0].mesh);
  REQUIRE(fixture.renderer.isValid(crateMesh));
  REQUIRE(fixture.renderer.submeshes(crateMesh).size() == 1);
  const core::Uuid woodMaterialUuid = core::Uuid::derive(crateUuid, "material/0");
  const renderer::MaterialHandle woodMaterial = database.material(woodMaterialUuid);
  REQUIRE(fixture.renderer.isValid(woodMaterial));
  // The glTF's image was cooked and uploaded, and the material reads it.
  const core::Uuid imageUuid = core::Uuid::derive(crateUuid, "image/0");
  REQUIRE(fixture.renderer.isValid(database.texture(imageUuid)));
  REQUIRE(fixture.renderer.material(woodMaterial).baseColorTexture == database.texture(imageUuid));
  REQUIRE(fixture.renderer.material(woodMaterial).doubleSided);

  const core::Uuid skyUuid = byName(database, "sky", AssetType::Environment)->uuid;
  REQUIRE(fixture.renderer.isValid(database.environment(skyUuid)));
}

TEST_CASE("a changed source is re-imported by polling and materials follow their textures", "[assets][database]") {
  Fixture fixture;
  AssetDatabase database{fixture.renderer};
  database.open(fixture.root, fixture.roots);
  const core::Uuid woodUuid = database.findByPath(fixture.root / "assets" / "wood.png")->uuid;
  const core::Uuid paintedUuid = byName(database, "painted", AssetType::Material)->uuid;
  MaterialSource painted = *database.materialSource(paintedUuid);
  painted.baseColorTexture = woodUuid;
  database.setMaterialSource(paintedUuid, painted);
  const renderer::MaterialHandle paintedHandle = database.material(paintedUuid);
  const renderer::TextureHandle before = database.texture(woodUuid);
  REQUIRE(fixture.renderer.material(paintedHandle).baseColorTexture == before);
  REQUIRE(database.pollChanges().empty());

  // A larger image replaces the file: the texture is re-imported under the same identity, the
  // old handle is gone and the material now reads the new one.
  Fixture::touchLater(fixture.root / "assets" / "wood.png",
                      test::encodePng({4, 4}, std::vector<std::uint8_t>(4 * 4 * 4, 90)));
  std::this_thread::sleep_for(std::chrono::milliseconds{600});
  const std::vector<core::Uuid> changed = database.pollChanges();
  REQUIRE(changed == std::vector<core::Uuid>{woodUuid});
  const renderer::TextureHandle after = database.texture(woodUuid);
  REQUIRE(fixture.renderer.isValid(after));
  REQUIRE(after != before);
  REQUIRE(!fixture.renderer.isValid(before));
  REQUIRE(fixture.renderer.material(paintedHandle).baseColorTexture == after);

  // A changed material file updates the material in place.
  MaterialSource edited = painted;
  edited.metallic = 0.25f;
  const std::string editedText = saveMaterial(edited).dump(2);
  Fixture::touchLater(fixture.root / "assets" / "painted.material.json",
                      std::as_bytes(std::span{editedText.data(), editedText.size()}));
  std::this_thread::sleep_for(std::chrono::milliseconds{600});
  REQUIRE(database.pollChanges() == std::vector<core::Uuid>{paintedUuid});
  REQUIRE(database.material(paintedUuid) == paintedHandle);
  REQUIRE(fixture.renderer.material(paintedHandle).metallic == Approx(0.25f));

  // Settings are written to the sidecar and re-import at once.
  REQUIRE(database.setTextureSettings(woodUuid, TextureSettings{.srgb = false, .mipmaps = false}).has_value());
  REQUIRE(database.textureSettings(woodUuid).srgb == false);
  REQUIRE(fixture.renderer.isValid(database.texture(woodUuid)));
  REQUIRE(database.texture(woodUuid) != after);
  {
    AssetDatabase reopened{fixture.renderer};
    reopened.open(fixture.root, fixture.roots);
    REQUIRE(reopened.textureSettings(woodUuid).mipmaps == false);
  }
}

TEST_CASE("a script is read on first use and again, with a new revision, when it changes", "[assets][database]") {
  Fixture fixture;
  const std::string first = "return { speed = 1 }\n";
  REQUIRE(core::writeFile(fixture.root / "assets" / "mover.lua", std::as_bytes(std::span{first.data(), first.size()}))
              .has_value());
  AssetDatabase database{fixture.renderer};
  database.open(fixture.root, fixture.roots);
  const AssetInfo *info = byName(database, "mover", AssetType::Script);
  REQUIRE(info != nullptr);
  REQUIRE(toString(info->type) == "Script");
  const core::Uuid uuid = info->uuid;
  const ScriptSource *loaded = database.script(uuid);
  REQUIRE(loaded != nullptr);
  REQUIRE(loaded->code == first);
  const std::uint64_t revision = loaded->revision;
  REQUIRE(database.script(uuid)->revision == revision);
  REQUIRE(database.script(core::Uuid::generate()) == nullptr);

  const std::string second = "return { speed = 2 }\n";
  Fixture::touchLater(fixture.root / "assets" / "mover.lua", std::as_bytes(std::span{second.data(), second.size()}));
  std::this_thread::sleep_for(std::chrono::milliseconds{600});
  REQUIRE(database.pollChanges() == std::vector<core::Uuid>{uuid});
  REQUIRE(database.script(uuid)->code == second);
  REQUIRE(database.script(uuid)->revision > revision);

  const auto created = database.createScript(fixture.root / "assets" / "scripts" / "new.lua", "return {}\n");
  REQUIRE(created.has_value());
  REQUIRE(database.find(*created)->type == AssetType::Script);
  REQUIRE(database.script(*created)->code == "return {}\n");
  REQUIRE(std::filesystem::exists(fixture.root / "assets" / "scripts" / "new.lua.meta"));
  REQUIRE(!database.createScript(fixture.root / "assets" / "new.txt", "").has_value());
  REQUIRE(std::ranges::equal(database.roots(), fixture.roots));
}

TEST_CASE("a sound is read on first use and again, with a new revision, when it changes", "[assets][database]") {
  Fixture fixture;
  const std::vector<std::byte> first(64, std::byte{1});
  REQUIRE(core::writeFile(fixture.root / "assets" / "chime.wav", first).has_value());
  REQUIRE(core::writeFile(fixture.root / "assets" / "notes.txt", first).has_value());
  AssetDatabase database{fixture.renderer};
  database.open(fixture.root, fixture.roots);
  const AssetInfo *info = byName(database, "chime", AssetType::Sound);
  REQUIRE(info != nullptr);
  REQUIRE(database.assets(AssetType::Sound).size() == 1); // the text file is no asset
  REQUIRE(assetTypeFromString("Sound") == AssetType::Sound);
  const core::Uuid uuid = info->uuid;
  const SoundSource *loaded = database.sound(uuid);
  REQUIRE(loaded != nullptr);
  REQUIRE(loaded->bytes == first);
  const std::uint64_t revision = loaded->revision;
  REQUIRE(database.sound(uuid)->revision == revision);
  REQUIRE(database.sound(builtin::box()) == nullptr);

  const std::vector<std::byte> second(32, std::byte{2});
  Fixture::touchLater(fixture.root / "assets" / "chime.wav", second);
  std::this_thread::sleep_for(std::chrono::milliseconds{600});
  REQUIRE(database.pollChanges() == std::vector<core::Uuid>{uuid});
  REQUIRE(database.sound(uuid)->bytes == second);
  REQUIRE(database.sound(uuid)->revision > revision);
}

TEST_CASE("a glTF file's skins and clips are sub-assets, reloaded with the file", "[assets][database]") {
  Fixture fixture;
  test::writeSkinnedGltf(fixture.root / "assets" / "models" / "rig.gltf");
  // A sidecar from before skins were sub-assets lists none and is rebuilt.
  const core::Uuid rigUuid = core::Uuid::generate();
  const nlohmann::json old{{"version", 1},
                           {"uuid", rigUuid.toString()},
                           {"type", "Model"},
                           {"settings", nlohmann::json::object()},
                           {"subAssets", nlohmann::json::array()}};
  REQUIRE(core::writeFile(fixture.root / "assets" / "models" / "rig.gltf.meta", old.dump()).has_value());
  AssetDatabase database{fixture.renderer};
  database.open(fixture.root, fixture.roots);
  const AssetInfo *skinInfo = byName(database, "StripSkin", AssetType::Skin);
  const AssetInfo *clipInfo = byName(database, "Bend", AssetType::Animation);
  REQUIRE(skinInfo != nullptr);
  REQUIRE(clipInfo != nullptr);
  REQUIRE(skinInfo->parent == rigUuid);
  REQUIRE(skinInfo->uuid == core::Uuid::derive(rigUuid, "skin/0"));
  REQUIRE(clipInfo->uuid == core::Uuid::derive(rigUuid, "animation/0"));

  const Skin *skin = database.skin(skinInfo->uuid);
  REQUIRE(skin != nullptr);
  REQUIRE(skin->joints.size() == 2);
  const AnimationClip *clip = database.animation(clipInfo->uuid);
  REQUIRE(clip != nullptr);
  REQUIRE(clip->duration == Approx(1.0f));
  REQUIRE(database.animation(skinInfo->uuid) == nullptr); // not a clip
  const Model *model = database.model(rigUuid);
  REQUIRE(model != nullptr);
  REQUIRE(model->animations == std::vector<core::Uuid>{clipInfo->uuid});
  REQUIRE(model->nodes[3].skin == skinInfo->uuid);
  REQUIRE(model->nodes[0].skin.isNil());
  REQUIRE(!database.meshData(model->nodes[3].mesh)->skin.empty());

  const std::uint64_t revision = clip->revision;
  REQUIRE(database.reimport(rigUuid).has_value());
  REQUIRE(database.animation(clipInfo->uuid)->revision > revision);
  REQUIRE(database.skin(skinInfo->uuid)->revision > revision);
}

TEST_CASE("mesh data stays on the CPU for built-in and glTF meshes", "[assets][database]") {
  Fixture fixture;
  AssetDatabase database{fixture.renderer};
  database.open(fixture.root, fixture.roots);
  const renderer::MeshData *box = database.meshData(builtin::box());
  REQUIRE(box != nullptr);
  REQUIRE(box->triangleCount() == 12);
  const AssetInfo *crateMesh = nullptr;
  for (const AssetInfo *mesh : database.assets(AssetType::Mesh)) {
    if (mesh->source != "builtin") {
      crateMesh = mesh;
    }
  }
  REQUIRE(crateMesh != nullptr);
  const renderer::MeshData *crate = database.meshData(crateMesh->uuid);
  REQUIRE(crate != nullptr);
  REQUIRE(!crate->indices.empty());
  REQUIRE(database.meshData(core::Uuid::generate()) == nullptr);
}

TEST_CASE("materials are created, edited and saved as files", "[assets][database]") {
  Fixture fixture;
  AssetDatabase database{fixture.renderer};
  database.open(fixture.root, fixture.roots);
  MaterialSource metal;
  metal.metallic = 1.0f;
  metal.roughness = 0.2f;
  const auto created = database.createMaterial(fixture.root / "assets" / "metal.material.json", metal);
  REQUIRE(created.has_value());
  REQUIRE(byName(database, "metal", AssetType::Material) != nullptr);
  const renderer::MaterialHandle handle = database.material(*created);
  REQUIRE(fixture.renderer.material(handle).roughness == Approx(0.2f));
  metal.roughness = 0.9f;
  database.setMaterialSource(*created, metal);
  REQUIRE(fixture.renderer.material(handle).roughness == Approx(0.9f));
  REQUIRE(database.saveMaterial(*created).has_value());
  const auto bytes = core::readFile(fixture.root / "assets" / "metal.material.json");
  REQUIRE(bytes.has_value());
  const auto reloaded = loadMaterial(nlohmann::json::parse(bytes->begin(), bytes->end()));
  REQUIRE(reloaded.has_value());
  REQUIRE(reloaded->roughness == Approx(0.9f));
  REQUIRE(!database.createMaterial(fixture.root / "assets" / "wrong.json", metal).has_value());
  // A glTF material is not a file: saving it is refused.
  const core::Uuid crateUuid = byName(database, "crate", AssetType::Model)->uuid;
  REQUIRE(!database.saveMaterial(core::Uuid::derive(crateUuid, "material/0")).has_value());
}

TEST_CASE("material files round-trip and refuse unknown versions", "[assets][material]") {
  MaterialSource source;
  source.baseColor = {0.1f, 0.2f, 0.3f, 0.4f};
  source.emissive = {1.0f, 0.5f, 0.0f};
  source.alphaMode = renderer::AlphaMode::Mask;
  source.wrap = renderer::TextureWrap::MirroredRepeat;
  source.normalTexture = core::Uuid::generate();
  const nlohmann::json document = saveMaterial(source);
  REQUIRE(document["version"] == MaterialFileVersion);
  REQUIRE(document["alphaMode"] == "Mask");
  const auto loaded = loadMaterial(document);
  REQUIRE(loaded.has_value());
  REQUIRE(*loaded == source);
  nlohmann::json future = document;
  future["version"] = MaterialFileVersion + 1;
  REQUIRE(!loadMaterial(future).has_value());
  REQUIRE(!loadMaterial(nlohmann::json::array()).has_value());
}

TEST_CASE("the built-in mesh identities never change, since scene files hold them", "[assets][database]") {
  REQUIRE(builtin::box().toString() == "a464f023-844b-8938-9df4-75487155e36d");
  REQUIRE(builtin::sphere().toString() == "fe3d3b79-c4ee-8f48-8944-6b1596b2c621");
  REQUIRE(builtin::plane().toString() == "bb8714b8-63f4-81c4-86ef-ad99a6ff9d40");
  REQUIRE(builtin::cylinder().toString() == "4982c24f-bd36-8c92-a2c1-47e047fbe1b6");
  REQUIRE(builtin::capsule().toString() == "663df5e4-bf07-8eb4-9364-2db2a5a3e28d");
}
