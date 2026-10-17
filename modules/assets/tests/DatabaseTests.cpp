#include "AssetTestSupport.h"

#include <sonnet/assets/AssetDatabase.h>
#include <sonnet/assets/Json.h>
#include <sonnet/core/JobSystem.h>

#include <sonnet/platform/Platform.h>
#include <sonnet/renderer/Renderer.h>
#include <sonnet/rhi/NullDevice.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <format>
#include <set>
#include <thread>

using namespace sonnet;
using namespace sonnet::assets;
using Catch::Approx;

namespace {

struct Fixture {
  platform::Platform platform{{.headless = true}};
  std::unique_ptr<rhi::NullDevice> device = rhi::createNullDevice();
  renderer::Renderer renderer{*device, platform.basePath() / "shaders"};
  core::JobSystem jobs{{.workerCount = 2}};
  std::filesystem::path root = test::freshDirectory("sonnet_assets_db");
  std::vector<std::string> roots{"assets"};

  Fixture() {
    std::filesystem::create_directories(root / "assets" / "models");
    REQUIRE(core::writeFile(root / "assets" / "wood.png", test::encodePng({2, 2}, test::quadPixels())).has_value());
    REQUIRE(core::writeFile(root / "assets" / "models" / "wood.png", test::encodePng({2, 2}, test::quadPixels()))
                .has_value());
    test::writeBoxGltf(root / "assets" / "models" / "crate.gltf", "wood.png");
    REQUIRE(core::writeFile(root / "assets" / "sky.hdr",
                            test::encodeHdr({4, 2}, std::vector<float>(std::size_t{4} * 2 * 3, 0.5f)))
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

TEST_CASE("a sidecar from a newer engine is skipped and left as it was", "[assets][database]") {
  Fixture fixture;
  {
    AssetDatabase database{fixture.renderer, fixture.jobs};
    database.open(fixture.root, fixture.roots);
  }
  const std::filesystem::path sidecar = fixture.root / "assets" / "wood.png.meta";
  const std::string newer = R"({"version": 99, "uuid": "9e1f0c34-5a7b-4d2e-8f10-2b3c4d5e6f70", "futureField": true})";
  REQUIRE(core::writeFile(sidecar, std::string_view{newer}).has_value());

  AssetDatabase database{fixture.renderer, fixture.jobs};
  database.open(fixture.root, fixture.roots);
  REQUIRE(database.findByPath(fixture.root / "assets" / "wood.png") == nullptr);
  // The other assets are unaffected, and the file keeps the fields this engine does not know.
  REQUIRE(byName(database, "crate", AssetType::Model) != nullptr);
  const auto after = core::readFile(sidecar);
  REQUIRE(after.has_value());
  REQUIRE(std::string_view{reinterpret_cast<const char *>(after->data()), after->size()} == newer);
}

TEST_CASE("opening a project writes sidecars and keeps identities across reopens", "[assets][database]") {
  Fixture fixture;
  core::Uuid woodUuid;
  core::Uuid crateUuid;
  core::Uuid crateMeshUuid;
  {
    AssetDatabase database{fixture.renderer, fixture.jobs};
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
    REQUIRE(database.assets(AssetType::Mesh).size() == builtin::all().size() + 1); // the built-ins and the crate
  }
  {
    AssetDatabase database{fixture.renderer, fixture.jobs};
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
  AssetDatabase database{fixture.renderer, fixture.jobs};
  database.open(fixture.root, fixture.roots);

  const renderer::MeshHandle box = database.mesh(builtin::box());
  REQUIRE(fixture.renderer.isValid(box));
  REQUIRE(database.mesh(builtin::box()) == box);
  REQUIRE(!database.mesh(core::Uuid::generate()).isValid());

  // Every built-in is registered, distinct, and builds its mesh and the data physics reads.
  std::set<core::Uuid> seen;
  for (const builtin::Entry &entry : builtin::all()) {
    REQUIRE(seen.insert(entry.uuid()).second);
    REQUIRE(database.find(entry.uuid()) != nullptr);
    REQUIRE(database.find(entry.uuid())->name == entry.name);
    REQUIRE(fixture.renderer.isValid(database.mesh(entry.uuid())));
    const renderer::MeshData *data = database.meshData(entry.uuid());
    REQUIRE(data != nullptr);
    REQUIRE(data->triangleCount() > 0);
  }

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
  AssetDatabase database{fixture.renderer, fixture.jobs};
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
                      test::encodePng({4, 4}, std::vector<std::uint8_t>(std::size_t{4} * 4 * 4, 90)));
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
    AssetDatabase reopened{fixture.renderer, fixture.jobs};
    reopened.open(fixture.root, fixture.roots);
    REQUIRE(reopened.textureSettings(woodUuid).mipmaps == false);
  }
}

TEST_CASE("a script is read on first use and again, with a new revision, when it changes", "[assets][database]") {
  Fixture fixture;
  const std::string first = "return { speed = 1 }\n";
  REQUIRE(core::writeFile(fixture.root / "assets" / "mover.lua", std::as_bytes(std::span{first.data(), first.size()}))
              .has_value());
  AssetDatabase database{fixture.renderer, fixture.jobs};
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
  AssetDatabase database{fixture.renderer, fixture.jobs};
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
  AssetDatabase database{fixture.renderer, fixture.jobs};
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

TEST_CASE("every re-import of a model is reported once, loaded or not", "[assets][database]") {
  Fixture fixture;
  AssetDatabase database{fixture.renderer, fixture.jobs};
  database.open(fixture.root, fixture.roots);
  const auto models = database.assets(AssetType::Model);
  REQUIRE(!models.empty());
  const core::Uuid uuid = models.front()->uuid;
  REQUIRE(database.takeReimportedModels().empty());

  // Only the hierarchy is loaded, as a prefab needs it.
  REQUIRE(database.model(uuid) != nullptr);
  REQUIRE(database.reimport(uuid).has_value());
  REQUIRE(database.takeReimportedModels() == std::vector<core::Uuid>{uuid});
  REQUIRE(database.takeReimportedModels().empty());

  REQUIRE(database.setModelSettings(uuid, ModelSettings{.lightScale = 0.5f}).has_value());
  REQUIRE(database.takeReimportedModels() == std::vector<core::Uuid>{uuid});
}

TEST_CASE("mesh data stays on the CPU for built-in and glTF meshes", "[assets][database]") {
  Fixture fixture;
  AssetDatabase database{fixture.renderer, fixture.jobs};
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
  AssetDatabase database{fixture.renderer, fixture.jobs};
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
  const auto reloaded = loadMaterial(parseJson(*bytes));
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
  source.baseColorSampler.wrapS = renderer::TextureWrap::MirroredRepeat;
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

TEST_CASE("material files round-trip texture transforms and read version 1", "[assets][material]") {
  MaterialSource source;
  source.baseColorTransform = {.offset = {0.25f, -0.5f}, .rotation = 1.5f, .scale = {2.0f, 3.0f}};
  source.emissiveTransform = {.offset = {0.0f, 0.0f}, .rotation = 0.0f, .scale = {-1.0f, 1.0f}};
  const nlohmann::json document = saveMaterial(source);
  // Only the slots that move are written.
  REQUIRE(document["transforms"].size() == 2);
  REQUIRE(document["transforms"]["baseColor"]["rotation"] == 1.5f);
  const auto loaded = loadMaterial(document);
  REQUIRE(loaded.has_value());
  REQUIRE(*loaded == source);

  // A version 1 file has no transforms and loads with the identity in every slot.
  nlohmann::json older = saveMaterial(MaterialSource{});
  older["version"] = 1;
  older.erase("transforms");
  const auto fromOlder = loadMaterial(older);
  REQUIRE(fromOlder.has_value());
  REQUIRE(*fromOlder == MaterialSource{});
}

TEST_CASE("material files round-trip each slot's UV set and read version 2 on set 0", "[assets][material]") {
  MaterialSource source;
  source.occlusionTransform.texCoord = 1;
  source.normalTransform = {.offset = {0.5f, 0.0f}, .rotation = 0.0f, .scale = {1.0f, 1.0f}, .texCoord = 1};
  const nlohmann::json document = saveMaterial(source);
  // A slot on the second set is written even when it does not move.
  REQUIRE(document["transforms"].size() == 2);
  REQUIRE(document["transforms"]["occlusion"]["texCoord"] == 1);
  const auto loaded = loadMaterial(document);
  REQUIRE(loaded.has_value());
  REQUIRE(*loaded == source);

  // A version 2 file names no set: every slot reads the first.
  nlohmann::json older = document;
  older["version"] = 2;
  older["transforms"]["occlusion"].erase("texCoord");
  older["transforms"]["normal"].erase("texCoord");
  const auto fromOlder = loadMaterial(older);
  REQUIRE(fromOlder.has_value());
  REQUIRE(fromOlder->occlusionTransform.texCoord == 0);
  REQUIRE(fromOlder->normalTransform.texCoord == 0);
  REQUIRE(fromOlder->normalTransform.offset == glm::vec2{0.5f, 0.0f});
}

TEST_CASE("material files round-trip the unlit flag and read version 3 as lit", "[assets][material]") {
  MaterialSource source;
  source.unlit = true;
  const nlohmann::json document = saveMaterial(source);
  REQUIRE(document["unlit"] == true);
  const auto loaded = loadMaterial(document);
  REQUIRE(loaded.has_value());
  REQUIRE(*loaded == source);

  // A version 3 file has no unlit field: it loads lit.
  nlohmann::json older = document;
  older["version"] = 3;
  older.erase("unlit");
  const auto fromOlder = loadMaterial(older);
  REQUIRE(fromOlder.has_value());
  REQUIRE(*fromOlder == MaterialSource{});
}

TEST_CASE("the built-in mesh identities never change, since scene files hold them", "[assets][database]") {
  REQUIRE(builtin::box().toString() == "a464f023-844b-8938-9df4-75487155e36d");
  REQUIRE(builtin::sphere().toString() == "fe3d3b79-c4ee-8f48-8944-6b1596b2c621");
  REQUIRE(builtin::plane().toString() == "bb8714b8-63f4-81c4-86ef-ad99a6ff9d40");
  REQUIRE(builtin::cylinder().toString() == "4982c24f-bd36-8c92-a2c1-47e047fbe1b6");
  REQUIRE(builtin::capsule().toString() == "663df5e4-bf07-8eb4-9364-2db2a5a3e28d");
  REQUIRE(builtin::cone().toString() == "c4b13e29-7d02-88c3-bfe9-af058a179518");
  REQUIRE(builtin::torus().toString() == "207411e3-2d40-801a-b153-bf3b0c8546e8");
  REQUIRE(builtin::ramp().toString() == "53ca34cf-0b66-8103-9462-0a00fb9fbc2d");
  REQUIRE(builtin::stairs().toString() == "f1fa4e76-0b71-8884-8ace-6230f9c3d074");
  REQUIRE(builtin::hemisphere().toString() == "abc845f9-54d0-8db4-9c0f-d2dcc45a7211");
  REQUIRE(builtin::arch().toString() == "1ff1a00b-8fc2-8825-8cfa-fd4c78a70efe");
  REQUIRE(builtin::icosphere().toString() == "edd73868-8c8d-837e-9c97-f8de1a22a52a");
}

TEST_CASE("a requested texture arrives on a later frame, imported off the main thread", "[assets][database]") {
  Fixture fixture;
  AssetDatabase database{fixture.renderer, fixture.jobs};
  database.open(fixture.root, fixture.roots);

  const core::Uuid woodUuid = database.findByPath(fixture.root / "assets" / "wood.png")->uuid;
  REQUIRE(!database.requestTexture(woodUuid).isValid()); // nothing yet, and an import is scheduled
  REQUIRE(database.loading());
  // Asking again while it is in flight must not schedule a second import of the same file.
  REQUIRE(!database.requestTexture(woodUuid).isValid());

  database.waitForLoads();
  REQUIRE(!database.loading());
  const renderer::TextureHandle wood = database.requestTexture(woodUuid);
  REQUIRE(fixture.renderer.isValid(wood));
  // The same object the synchronous loader would have returned, under the same identity.
  REQUIRE(database.texture(woodUuid) == wood);
  REQUIRE(std::filesystem::exists(database.cacheDirectory() / (woodUuid.toString() + ".ktx2")));
}

TEST_CASE("a requested glTF mesh brings its whole file with it", "[assets][database]") {
  Fixture fixture;
  AssetDatabase database{fixture.renderer, fixture.jobs};
  database.open(fixture.root, fixture.roots);

  const core::Uuid crateUuid = byName(database, "crate", AssetType::Model)->uuid;
  const core::Uuid meshUuid = core::Uuid::derive(crateUuid, "mesh/0");
  const core::Uuid imageUuid = core::Uuid::derive(crateUuid, "image/0");

  REQUIRE(!database.requestMesh(meshUuid).isValid());
  REQUIRE(database.loading());
  // A texture of the same file joins the request already in flight rather than starting another.
  REQUIRE(!database.requestTexture(imageUuid).isValid());
  database.waitForLoads();

  // One request, and the mesh, its material, its image and the model are all there.
  REQUIRE(fixture.renderer.isValid(database.requestMesh(meshUuid)));
  REQUIRE(fixture.renderer.isValid(database.requestTexture(imageUuid)));
  REQUIRE(fixture.renderer.isValid(database.material(core::Uuid::derive(crateUuid, "material/0"))));
  REQUIRE(database.model(crateUuid) != nullptr);
  REQUIRE(database.meshData(meshUuid) != nullptr);
}

// Every model in a project is a prefab, placed at project open, and a prefab needs only the node
// hierarchy. Reading it must not import the file: that was the stall roadmap.md recorded.
TEST_CASE("a model's hierarchy is read without importing its meshes or images", "[assets][database]") {
  Fixture fixture;
  test::writeSkinnedGltf(fixture.root / "assets" / "models" / "rig.gltf");
  AssetDatabase database{fixture.renderer, fixture.jobs};
  database.open(fixture.root, fixture.roots);
  const std::size_t buffersBefore = fixture.device->bufferCount();

  const core::Uuid crateUuid = byName(database, "crate", AssetType::Model)->uuid;
  const core::Uuid rigUuid = byName(database, "rig", AssetType::Model)->uuid;
  const Model *crate = database.model(crateUuid);
  const Model *rig = database.model(rigUuid);
  REQUIRE(crate != nullptr);
  REQUIRE(rig != nullptr);
  REQUIRE(crate->nodes.size() == 1);
  REQUIRE(crate->nodes[0].mesh == core::Uuid::derive(crateUuid, "mesh/0"));
  REQUIRE(rig->nodes[3].skin == core::Uuid::derive(rigUuid, "skin/0"));
  REQUIRE(rig->animations == std::vector<core::Uuid>{core::Uuid::derive(rigUuid, "animation/0")});
  // Nothing uploaded, nothing cooked, nothing scheduled.
  REQUIRE(fixture.device->bufferCount() == buffersBefore);
  REQUIRE_FALSE(std::filesystem::exists(database.cacheDirectory() /
                                        (core::Uuid::derive(crateUuid, "image/0").toString() + ".ktx2")));
  REQUIRE_FALSE(database.loading());

  // A caller may hold a node while asking for its mesh, and that mesh's import is the file's: the
  // hierarchy has to stay where it is when the rest of the file arrives, not be replaced by an
  // equal one in new storage. The sanitizer found the version that replaced it, through exactly
  // this call, as a use after free.
  const ModelNode *firstNode = crate->nodes.data();
  REQUIRE(fixture.renderer.isValid(database.mesh(firstNode->mesh)));
  REQUIRE(database.model(crateUuid)->nodes.data() == firstNode);

  // The full import builds the same hierarchy, node for node, so a prefab placed from the first
  // matches the file once it is in.
  const std::vector<ModelNode> read = rig->nodes;
  const std::vector<core::Uuid> readClips = rig->animations;
  REQUIRE(database.skin(core::Uuid::derive(rigUuid, "skin/0")) != nullptr);
  REQUIRE(fixture.device->bufferCount() > buffersBefore);
  const Model *imported = database.model(rigUuid);
  REQUIRE(imported->animations == readClips);
  REQUIRE(imported->nodes.size() == read.size());
  for (std::size_t n = 0; n < read.size(); ++n) {
    CAPTURE(n);
    REQUIRE(imported->nodes[n].name == read[n].name);
    REQUIRE(imported->nodes[n].parent == read[n].parent);
    REQUIRE(imported->nodes[n].position == read[n].position);
    REQUIRE(imported->nodes[n].rotation == read[n].rotation);
    REQUIRE(imported->nodes[n].scale == read[n].scale);
    REQUIRE(imported->nodes[n].mesh == read[n].mesh);
    REQUIRE(imported->nodes[n].skin == read[n].skin);
  }
}

TEST_CASE("a model's light scale lives in its sidecar, and changing it re-imports the lights", "[assets][database]") {
  Fixture fixture;
  // Blender's 1000 W point light, as its exporter writes it: 1000 * 683 / 4pi candela, no range.
  const std::filesystem::path file = fixture.root / "assets" / "models" / "lamp.gltf";
  const nlohmann::json document{
      {"asset", {{"version", "2.0"}}},
      {"extensionsUsed", {"KHR_lights_punctual"}},
      {"extensions",
       {{"KHR_lights_punctual", {{"lights", nlohmann::json::array({{{"type", "point"}, {"intensity", 54351.4}}})}}}}},
      {"scene", 0},
      {"scenes", nlohmann::json::array({{{"nodes", {0}}}})},
      {"nodes", nlohmann::json::array({{{"name", "Bulb"}, {"extensions", {{"KHR_lights_punctual", {{"light", 0}}}}}}})},
  };
  REQUIRE(core::writeFile(file, document.dump()).has_value());
  AssetDatabase database{fixture.renderer, fixture.jobs};
  database.open(fixture.root, fixture.roots);
  const core::Uuid lampUuid = byName(database, "lamp", AssetType::Model)->uuid;
  const auto sidecar = [&] {
    const auto bytes = core::readFile(file.string() + ".meta");
    REQUIRE(bytes.has_value());
    return parseJson(*bytes);
  };

  // A new sidecar writes the default, and Blender's light comes out at about 80, reaching about 89 m.
  REQUIRE(sidecar()["settings"]["lightScale"].get<float>() == Approx(1.0f / 683.0f));
  REQUIRE(database.modelSettings(lampUuid) == ModelSettings{});
  const Model *lamp = database.model(lampUuid);
  REQUIRE(lamp != nullptr);
  REQUIRE(lamp->nodes[0].light->intensity == Approx(54351.4f / 683.0f));
  REQUIRE(lamp->nodes[0].light->range == Approx(89.2f).epsilon(0.001));

  // Setting the scale rewrites the sidecar and re-imports the hierarchy with the new lights.
  REQUIRE(database.setModelSettings(lampUuid, ModelSettings{.lightScale = 1.0f}).has_value());
  REQUIRE(sidecar()["settings"]["lightScale"].get<float>() == 1.0f);
  REQUIRE(database.modelSettings(lampUuid).lightScale == 1.0f);
  lamp = database.model(lampUuid);
  REQUIRE(lamp->nodes[0].light->intensity == Approx(54351.4f));
  REQUIRE(lamp->nodes[0].light->range == Approx(100.0f));

  // Only a model has these settings.
  const core::Uuid woodUuid = database.findByPath(fixture.root / "assets" / "wood.png")->uuid;
  REQUIRE_FALSE(database.setModelSettings(woodUuid, ModelSettings{}).has_value());
}

TEST_CASE("a requested skin or clip brings its whole file with it", "[assets][database]") {
  Fixture fixture;
  test::writeSkinnedGltf(fixture.root / "assets" / "models" / "rig.gltf");
  AssetDatabase database{fixture.renderer, fixture.jobs};
  database.open(fixture.root, fixture.roots);
  const core::Uuid rigUuid = byName(database, "rig", AssetType::Model)->uuid;
  const core::Uuid skinUuid = core::Uuid::derive(rigUuid, "skin/0");
  const core::Uuid clipUuid = core::Uuid::derive(rigUuid, "animation/0");

  REQUIRE(database.requestSkin(skinUuid) == nullptr);
  REQUIRE(database.loading());
  REQUIRE(database.requestAnimation(clipUuid) == nullptr); // joins the request in flight
  database.waitForLoads();

  REQUIRE(database.requestSkin(skinUuid) != nullptr);
  REQUIRE(database.requestAnimation(clipUuid) != nullptr);
  REQUIRE(database.requestSkin(clipUuid) == nullptr); // not a skin
  REQUIRE(fixture.renderer.isValid(database.requestMesh(database.model(rigUuid)->nodes[3].mesh)));
  REQUIRE_FALSE(database.loading());
}

TEST_CASE("requesting a built-in or a missing asset needs no job", "[assets][database]") {
  Fixture fixture;
  AssetDatabase database{fixture.renderer, fixture.jobs};
  database.open(fixture.root, fixture.roots);

  // A built-in is already in memory, so it comes back at once rather than a frame later.
  REQUIRE(fixture.renderer.isValid(database.requestMesh(builtin::box())));
  REQUIRE(!database.loading());
  REQUIRE(!database.requestMesh(core::Uuid::generate()).isValid());
  REQUIRE(!database.requestTexture({}).isValid());
  REQUIRE(!database.loading());
}

TEST_CASE("closing while a request is in flight finishes it rather than racing it", "[assets][database]") {
  Fixture fixture;
  AssetDatabase database{fixture.renderer, fixture.jobs};
  database.open(fixture.root, fixture.roots);

  const core::Uuid crateUuid = byName(database, "crate", AssetType::Model)->uuid;
  REQUIRE(!database.requestMesh(core::Uuid::derive(crateUuid, "mesh/0")).isValid());
  REQUIRE(database.loading());
  database.close(); // must not leave a job writing into a closed database
  REQUIRE(!database.loading());
}

TEST_CASE("a request for a texture that cannot be imported fails once and is not retried", "[assets][database]") {
  Fixture fixture;
  AssetDatabase database{fixture.renderer, fixture.jobs};
  database.open(fixture.root, fixture.roots);

  const core::Uuid woodUuid = database.findByPath(fixture.root / "assets" / "wood.png")->uuid;
  // Truncate the source so the import fails, and clear the cooked copy that would otherwise serve it.
  std::filesystem::remove(database.cacheDirectory() / (woodUuid.toString() + ".ktx2"));
  Fixture::touchLater(fixture.root / "assets" / "wood.png", std::vector<std::byte>{std::byte{0x1}});

  REQUIRE(!database.requestTexture(woodUuid).isValid());
  database.waitForLoads();
  REQUIRE(!database.requestTexture(woodUuid).isValid());
  REQUIRE(!database.loading()); // marked failed, so asking again schedules nothing
}

// A material file used to import its textures in place on the frame that first drew it, the
// stall M8 removed for meshes (roadmap.md, "A pending texture draws as nothing in particular").
TEST_CASE("a material requests its textures and shows a placeholder until they arrive", "[assets][database]") {
  Fixture fixture;
  core::JobSystem jobs{{.workerCount = 0}}; // nothing runs until the test drains it
  core::Uuid woodUuid;
  core::Uuid brokenUuid;
  {
    AssetDatabase database{fixture.renderer, jobs};
    database.open(fixture.root, fixture.roots);
    woodUuid = database.findByPath(fixture.root / "assets" / "wood.png")->uuid;
  }
  REQUIRE(core::writeFile(fixture.root / "assets" / "broken.png", std::vector<std::byte>{std::byte{0x1}}).has_value());
  {
    AssetDatabase database{fixture.renderer, jobs};
    database.open(fixture.root, fixture.roots);
    brokenUuid = database.findByPath(fixture.root / "assets" / "broken.png")->uuid;
  }
  MaterialSource source;
  source.baseColorTexture = woodUuid;
  source.normalTexture = woodUuid;
  REQUIRE(core::writeFile(fixture.root / "assets" / "wooden.material.json", saveMaterial(source).dump(2)).has_value());
  source.baseColorTexture = brokenUuid;
  source.normalTexture = {};
  REQUIRE(core::writeFile(fixture.root / "assets" / "broken.material.json", saveMaterial(source).dump(2)).has_value());

  AssetDatabase database{fixture.renderer, jobs};
  database.open(fixture.root, fixture.roots);
  const std::filesystem::path cooked = database.cacheDirectory() / (woodUuid.toString() + ".ktx2");
  std::filesystem::remove(cooked);
  const renderer::MaterialHandle wooden = database.material(byName(database, "wooden", AssetType::Material)->uuid);
  const renderer::MaterialHandle broken = database.material(byName(database, "broken", AssetType::Material)->uuid);

  // Created at once, with nothing imported: the base colour reads the placeholder, the normal
  // slot the renderer's fallback, and the texture is a request in flight.
  REQUIRE(fixture.renderer.isValid(wooden));
  REQUIRE(database.loading());
  REQUIRE_FALSE(std::filesystem::exists(cooked));
  const renderer::TextureHandle placeholder = fixture.renderer.material(wooden).baseColorTexture;
  REQUIRE(fixture.renderer.isValid(placeholder));
  REQUIRE(fixture.renderer.textureIndex(placeholder) != fixture.renderer.textureIndex({}));
  REQUIRE_FALSE(fixture.renderer.material(wooden).normalTexture.isValid());
  REQUIRE(fixture.renderer.material(broken).baseColorTexture == placeholder); // one placeholder for all

  // The import and its main-thread publish update the materials in place.
  database.waitForLoads();
  const renderer::TextureHandle wood = database.requestTexture(woodUuid);
  REQUIRE(fixture.renderer.isValid(wood));
  REQUIRE(fixture.renderer.material(wooden).baseColorTexture == wood);
  REQUIRE(fixture.renderer.material(wooden).normalTexture == wood);
  // A texture that failed reads the white fallback, so it looks different from one still pending.
  REQUIRE_FALSE(fixture.renderer.material(broken).baseColorTexture.isValid());
}

// Reopening a project kept its file materials, which still read the textures the close had
// destroyed: the basic sample's ground lost its checker on the second open.
TEST_CASE("reopening a project resolves its materials against the new textures", "[assets][database]") {
  Fixture fixture;
  core::Uuid woodUuid;
  {
    AssetDatabase database{fixture.renderer, fixture.jobs};
    database.open(fixture.root, fixture.roots);
    woodUuid = database.findByPath(fixture.root / "assets" / "wood.png")->uuid;
  }
  MaterialSource source;
  source.baseColorTexture = woodUuid;
  REQUIRE(core::writeFile(fixture.root / "assets" / "wooden.material.json", saveMaterial(source).dump(2)).has_value());

  AssetDatabase database{fixture.renderer, fixture.jobs};
  for (int open = 0; open < 2; ++open) {
    database.open(fixture.root, fixture.roots);
    const renderer::MaterialHandle wooden = database.material(byName(database, "wooden", AssetType::Material)->uuid);
    database.waitForLoads();
    const renderer::TextureHandle wood = database.requestTexture(woodUuid);
    REQUIRE(fixture.renderer.isValid(wood));
    REQUIRE(fixture.renderer.material(wooden).baseColorTexture == wood);
  }
}

// What placing every model of the basic sample costs, measured rather than asserted: hidden from
// the default run, `assets_tests "[benchmark]"` prints the time to read each model's hierarchy,
// which is what opening a project now does, against the full import it used to do. The first
// import of a fresh copy also cooks the textures; the warm figure is every later open.
TEST_CASE("placing the basic sample's models reads hierarchies, not files", "[.][benchmark]") {
  platform::Platform platform{{.headless = true}};
  std::filesystem::path sample;
  for (std::filesystem::path base = std::filesystem::absolute(platform.basePath()); !base.empty();
       base = base.parent_path()) {
    if (std::filesystem::is_regular_file(base / "apps" / "samples" / "basic" / "project.json")) {
      sample = base / "apps" / "samples" / "basic";
      break;
    }
    if (base == base.root_path()) {
      break;
    }
  }
  if (sample.empty()) {
    SKIP("the sample was not found above " << platform.basePath().string());
  }
  const std::filesystem::path root = test::freshDirectory("sonnet_assets_benchmark");
  std::filesystem::copy(sample, root, std::filesystem::copy_options::recursive);
  const auto device = rhi::createNullDevice();
  renderer::Renderer renderer{*device, platform.basePath() / "shaders"};
  core::JobSystem jobs{{.workerCount = 0}};
  const std::vector<std::string> roots{"assets"};

  using Clock = std::chrono::steady_clock;
  const auto milliseconds = [](Clock::duration duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
  };
  // Each pass opens a fresh database, so nothing is in memory, and places every model the way the
  // editor and the player do: through model().
  std::size_t models = 0;
  const auto place = [&](bool import) {
    AssetDatabase database{renderer, jobs};
    database.open(root, roots);
    models = database.assets(AssetType::Model).size();
    const auto start = Clock::now();
    for (const AssetInfo *info : database.assets(AssetType::Model)) {
      const Model *model = database.model(info->uuid);
      REQUIRE(model != nullptr);
      if (import) {
        for (const ModelNode &node : model->nodes) {
          if (!node.mesh.isNil()) {
            static_cast<void>(database.mesh(node.mesh)); // the whole file, as model() once did
            break;
          }
        }
      }
    }
    const double elapsed = milliseconds(Clock::now() - start);
    database.close();
    return elapsed;
  };
  const double cold = place(true);
  double hierarchy = 1e9;
  double imported = 1e9;
  for (int run = 0; run < 5; ++run) {
    hierarchy = std::min(hierarchy, place(false));
    imported = std::min(imported, place(true));
  }
  WARN(std::format("{} models: hierarchy {:.3f} ms, full import {:.3f} ms warm and {:.3f} ms cold", models, hierarchy,
                   imported, cold));
  std::filesystem::remove_all(root);
}

TEST_CASE("a glTF file's dependencies are the files its URIs name, inside its folder", "[assets][gltf]") {
  Fixture fixture;
  const std::filesystem::path models = fixture.root / "assets" / "models";
  const auto dependencies = gltfDependencies(models / "crate.gltf");
  REQUIRE(dependencies.has_value());
  REQUIRE(*dependencies == std::vector<std::filesystem::path>{"crate.bin", "wood.png"});

  // Percent-encoded and in a subfolder, named twice, and a data URI that is no file.
  const std::string nested = R"({"asset": {"version": "2.0"},
    "buffers": [{"uri": "data:application/octet-stream;base64,AAAAAA==", "byteLength": 4}],
    "images": [{"uri": "textures/wood%20grain.png"}, {"uri": "textures/wood%20grain.png"}]})";
  REQUIRE(core::writeFile(models / "nested.gltf", nested).has_value());
  const auto nestedDependencies = gltfDependencies(models / "nested.gltf");
  REQUIRE(nestedDependencies.has_value());
  REQUIRE(*nestedDependencies == std::vector<std::filesystem::path>{"textures/wood grain.png"});

  const std::string outside = R"({"asset": {"version": "2.0"}, "images": [{"uri": "../wood.png"}]})";
  REQUIRE(core::writeFile(models / "outside.gltf", outside).has_value());
  REQUIRE_FALSE(gltfDependencies(models / "outside.gltf").has_value());
}

TEST_CASE("a file added under a root is registered, and unregistering forgets what it loaded", "[assets][database]") {
  Fixture fixture;
  std::filesystem::create_directories(fixture.root / "other");
  REQUIRE(
      core::writeFile(fixture.root / "other" / "stone.png", test::encodePng({2, 2}, test::quadPixels())).has_value());
  AssetDatabase database{fixture.renderer, fixture.jobs};
  database.open(fixture.root, fixture.roots);
  REQUIRE(AssetDatabase::sourceType("crate.GLTF") == AssetType::Model);
  REQUIRE(AssetDatabase::sourceType("sky.hdr") == AssetType::Environment);
  REQUIRE_FALSE(AssetDatabase::sourceType("crate.bin").has_value());

  // Outside every root, the next open would not find it, so it is refused.
  REQUIRE_FALSE(database.registerFile(fixture.root / "other" / "stone.png").has_value());
  std::filesystem::copy_file(fixture.root / "other" / "stone.png", fixture.root / "assets" / "stone.png");
  const auto stone = database.registerFile(fixture.root / "assets" / "stone.png");
  REQUIRE(stone.has_value());
  REQUIRE(std::filesystem::exists(fixture.root / "assets" / "stone.png.meta"));
  REQUIRE(database.registerFile(fixture.root / "assets" / "stone.png").value() == *stone); // already known

  // A material reading the texture falls back once the texture is forgotten.
  MaterialSource source;
  source.baseColorTexture = *stone;
  const auto stony = database.createMaterial(fixture.root / "assets" / "stony.material.json", source);
  REQUIRE(stony.has_value());
  const renderer::MaterialHandle material = database.material(*stony);
  database.waitForLoads();
  REQUIRE(fixture.renderer.material(material).baseColorTexture == database.texture(*stone));
  database.unregister(*stone);
  REQUIRE(database.find(*stone) == nullptr);
  REQUIRE_FALSE(database.texture(*stone).isValid());
  REQUIRE_FALSE(fixture.renderer.material(material).baseColorTexture.isValid());
  REQUIRE(std::filesystem::exists(fixture.root / "assets" / "stone.png")); // the file is left alone

  // A model goes with its sub-assets and their loaded objects, materials included.
  const AssetInfo *crate = byName(database, "crate", AssetType::Model);
  REQUIRE(crate != nullptr);
  const core::Uuid crateUuid = crate->uuid;
  const core::Uuid meshUuid = core::Uuid::derive(crateUuid, "mesh/0");
  const core::Uuid materialUuid = core::Uuid::derive(crateUuid, "material/0");
  REQUIRE(database.mesh(meshUuid).isValid());
  const renderer::MaterialHandle crateMaterial = database.material(materialUuid);
  REQUIRE(fixture.renderer.isValid(crateMaterial));
  // Through the database's own AssetInfo, as the editor's undo passes it: its entry is erased mid-call.
  database.unregister(crate->uuid);
  REQUIRE(database.find(crateUuid) == nullptr);
  REQUIRE(database.find(meshUuid) == nullptr);
  REQUIRE(database.find(materialUuid) == nullptr);
  REQUIRE_FALSE(database.requestMesh(meshUuid).isValid());
  REQUIRE_FALSE(fixture.renderer.isValid(crateMaterial));
  // Registering the file again brings the same identities back from its sidecar.
  REQUIRE(database.registerFile(fixture.root / "assets" / "models" / "crate.gltf").value() == crateUuid);
  REQUIRE(database.find(meshUuid) != nullptr);
}

TEST_CASE("material samplers round-trip per slot and migrate legacy wraps", "[assets][material][sampler]") {
  using namespace renderer;
  MaterialSource source;
  source.baseColorSampler = {.wrapS = TextureWrap::ClampToEdge,
                             .wrapT = TextureWrap::MirroredRepeat,
                             .magFilter = TextureFilter::Nearest,
                             .minFilter = TextureFilter::Linear,
                             .mipFilter = TextureMipFilter::None};
  source.normalSampler = {.wrapT = TextureWrap::ClampToEdge, .mipFilter = TextureMipFilter::Nearest};
  source.emissiveSampler.magFilter = TextureFilter::Nearest;
  const auto json = saveMaterial(source);
  REQUIRE(json["version"] == 5);
  REQUIRE_FALSE(json.contains("wrap"));
  REQUIRE_FALSE(json["samplers"].contains("occlusion"));
  REQUIRE(loadMaterial(json).value() == source);
  REQUIRE(saveMaterial(MaterialSource{})["samplers"].empty());
  for (int version = 1; version <= 4; ++version) {
    const auto migrated = loadMaterial(nlohmann::json{{"version", version}, {"wrap", "MirroredRepeat"}});
    REQUIRE(migrated.has_value());
    for (auto sampler : {migrated->baseColorSampler, migrated->metallicRoughnessSampler, migrated->normalSampler,
                         migrated->occlusionSampler, migrated->emissiveSampler}) {
      REQUIRE(sampler.wrapS == TextureWrap::MirroredRepeat);
      REQUIRE(sampler.wrapT == TextureWrap::MirroredRepeat);
      REQUIRE(sampler.magFilter == TextureFilter::Linear);
      REQUIRE(sampler.mipFilter == TextureMipFilter::Linear);
    }
  }
}

TEST_CASE("moving sources preserves identities and loaded objects", "[assets][database][move]") {
  Fixture fixture;
  AssetDatabase database{fixture.renderer, fixture.jobs};
  database.open(fixture.root, fixture.roots);
  const auto wood = fixture.root / "assets/wood.png";
  const auto id = database.findByPath(wood)->uuid;
  const auto handle = database.requestTexture(id);
  static_cast<void>(handle);
  const auto before = std::filesystem::last_write_time(wood);
  const auto to = fixture.root / "assets/renamed.png";
  REQUIRE(database.move(wood, to));
  REQUIRE_FALSE(database.loading());
  CHECK(database.find(id)->source == to);
  CHECK(database.find(id)->name == "renamed");
  CHECK(database.findByPath(to)->uuid == id);
  CHECK(std::filesystem::exists(to.string() + ".meta"));
  CHECK_FALSE(std::filesystem::exists(wood));
  CHECK((std::filesystem::last_write_time(to) == before));
  CHECK(database.pollChanges().empty());
  const auto texture = database.texture(id);
  REQUIRE(texture);
  REQUIRE(database.move(to, fixture.root / "assets/Renamed.png"));
  CHECK(database.texture(id) == texture);
  database.close();
  database.open(fixture.root, fixture.roots);
  CHECK(database.findByPath(fixture.root / "assets/Renamed.png")->uuid == id);
}

TEST_CASE("moving models and folders preserves every sub-asset", "[assets][database][move]") {
  Fixture fixture;
  AssetDatabase database{fixture.renderer, fixture.jobs};
  database.open(fixture.root, fixture.roots);
  const auto folder = fixture.root / "assets/models";
  const auto model = folder / "crate.gltf";
  const auto id = database.findByPath(model)->uuid;
  std::vector<core::Uuid> subs;
  for (const auto *info : database.assets()) {
    if (info->parent == id) {
      subs.push_back(info->uuid);
    }
  }
  REQUIRE_FALSE(subs.empty());
  CHECK_FALSE(database.move(model, fixture.root / "assets/crate.gltf"));
  REQUIRE(database.move(model, folder / "box.gltf"));
  for (const auto &sub : subs) {
    CHECK(database.find(sub)->source == folder / "box.gltf");
  }
  const auto to = fixture.root / "assets/objects";
  REQUIRE(database.move(folder, to));
  CHECK(database.find(id)->source == to / "box.gltf");
  for (const auto &sub : subs) {
    CHECK(database.find(sub)->source == to / "box.gltf");
  }
  CHECK(database.findByPath(to / "wood.png") != nullptr);
  CHECK(database.pollChanges().empty());
}

TEST_CASE("moves refuse invalid destinations without touching files", "[assets][database][move]") {
  Fixture fixture;
  AssetDatabase database{fixture.renderer, fixture.jobs};
  database.open(fixture.root, fixture.roots);
  const auto from = fixture.root / "assets/wood.png";
  const auto id = database.findByPath(from)->uuid;
  CHECK_FALSE(database.move(from, fixture.root / "wood.png"));
  CHECK_FALSE(database.move(from, fixture.root / "assets/models/wood.png"));
  CHECK_FALSE(database.move(from, fixture.root / "assets/wood.jpg"));
  CHECK_FALSE(database.move(from, fixture.root / "assets/missing/wood.png"));
  CHECK(std::filesystem::exists(from));
  CHECK(database.find(id)->source == from);
}

TEST_CASE("moved scripts reload under a new revision and unregister removes all cache variants",
          "[assets][database][move]") {
  Fixture fixture;
  AssetDatabase database{fixture.renderer, fixture.jobs};
  database.open(fixture.root, fixture.roots);
  const auto script = database.createScript(fixture.root / "assets/a.lua", "return {}");
  REQUIRE(script);
  const auto revision = database.script(*script)->revision;
  REQUIRE(database.move(fixture.root / "assets/a.lua", fixture.root / "assets/b.lua"));
  CHECK(database.script(*script)->revision > revision);
  const auto model = database.findByPath(fixture.root / "assets/models/crate.gltf")->uuid;
  std::vector<core::Uuid> ids{model};
  for (const auto *info : database.assets()) {
    if (info->parent == model) {
      ids.push_back(info->uuid);
    }
  }
  std::filesystem::create_directories(database.cacheDirectory());
  for (const auto &id : ids) {
    for (const auto *suffix : {".ktx2", ".astc.ktx2", ".hdr.ktx2"}) {
      REQUIRE(core::writeFile(database.cacheDirectory() / (id.toString() + suffix), "cache"));
    }
  }
  const auto unrelated = database.cacheDirectory() / "unrelated.ktx2";
  REQUIRE(core::writeFile(unrelated, "keep"));
  database.unregister(model);
  for (const auto &id : ids) {
    CHECK(database.find(id) == nullptr);
    for (const auto *suffix : {".ktx2", ".astc.ktx2", ".hdr.ktx2"}) {
      CHECK_FALSE(std::filesystem::exists(database.cacheDirectory() / (id.toString() + suffix)));
    }
  }
  CHECK(std::filesystem::exists(unrelated));
}

TEST_CASE("moving through a symlinked project path updates database metadata", "[assets][database][move]") {
  Fixture fixture;
  const auto alias = std::filesystem::path{fixture.root.string() + "-alias"};
  std::error_code error;
  std::filesystem::remove(alias, error);
  std::filesystem::create_directory_symlink(fixture.root, alias, error);
  if (error) {
    SKIP("directory symlinks unavailable: " << error.message());
  }
  AssetDatabase database{fixture.renderer, fixture.jobs};
  database.open(alias, fixture.roots);
  const auto from = alias / "assets/wood.png";
  const auto to = alias / "assets/renamed.png";
  const auto id = database.findByPath(from)->uuid;
  REQUIRE(database.move(from, to));
  CHECK(database.find(id)->source == to);
  REQUIRE(database.findByPath(to));
  CHECK(database.findByPath(to)->uuid == id);
  database.close();
  std::filesystem::remove(alias);
}

TEST_CASE("project folders can be renamed without changing asset roots", "[assets][database][move]") {
  Fixture fixture;
  AssetDatabase database{fixture.renderer, fixture.jobs};
  database.open(fixture.root, fixture.roots);
  const auto folder = fixture.root / "New folder";
  const auto renamed = fixture.root / "Renamed folder";
  std::filesystem::create_directory(folder);
  REQUIRE(core::writeFile(folder / "README", "plain project file"));
  REQUIRE(database.move(folder, renamed));
  CHECK(std::filesystem::exists(renamed / "README"));
  CHECK_FALSE(std::filesystem::exists(folder));
  REQUIRE(database.move(renamed, folder));
  CHECK(std::filesystem::exists(folder / "README"));
  CHECK_FALSE(database.move(fixture.root / "assets", fixture.root / "art"));
  CHECK_FALSE(database.move(fixture.root, fixture.root.parent_path() / "renamed-project"));
  CHECK_FALSE(database.move(folder, fixture.root / ".sonnet" / "folder"));
  CHECK(database.findByPath(fixture.root / "assets/wood.png") != nullptr);
  std::filesystem::create_directories(fixture.root / "content/imported");
  database.open(fixture.root, std::vector<std::string>{"content/imported"});
  CHECK_FALSE(database.move(fixture.root / "content", fixture.root / "renamed-content"));
  CHECK(std::filesystem::is_directory(fixture.root / "content/imported"));
}

TEST_CASE("rescan registers added files, unregisters removed ones and follows moves", "[assets][database][rescan]") {
  Fixture fixture;
  AssetDatabase database{fixture.renderer, fixture.jobs};
  database.open(fixture.root, fixture.roots);
  const auto assets = fixture.root / "assets";
  const auto wood = assets / "wood.png";
  const auto sky = assets / "sky.hdr";
  const auto crate = assets / "models/crate.gltf";
  const auto woodId = database.findByPath(wood)->uuid;
  const auto skyId = database.findByPath(sky)->uuid;
  const auto crateId = database.findByPath(crate)->uuid;
  const auto texture = database.texture(woodId);
  REQUIRE(texture);
  CHECK(database.rescan().empty());

  SECTION("a file that appeared is registered") {
    REQUIRE(core::writeFile(assets / "added.png", test::encodePng({2, 2}, test::quadPixels())).has_value());
    const auto result = database.rescan();
    REQUIRE(result.added.size() == 1);
    CHECK(result.removed.empty());
    CHECK(result.moved.empty());
    const AssetInfo *info = database.findByPath(assets / "added.png");
    REQUIRE(info != nullptr);
    CHECK(result.added[0].uuid == info->uuid);
    CHECK(result.added[0].type == AssetType::Texture);
    CHECK(std::filesystem::exists(assets / "added.png.meta"));
    CHECK(database.rescan().empty());
  }

  SECTION("a file that is gone is unregistered, with what hangs on it") {
    std::filesystem::remove(sky);
    std::filesystem::remove(crate);
    const auto result = database.rescan();
    REQUIRE(result.removed.size() == 2);
    CHECK(result.added.empty());
    CHECK(database.find(skyId) == nullptr);
    CHECK(database.find(crateId) == nullptr);
    CHECK(database.find(woodId) != nullptr);
    CHECK(database.texture(woodId) == texture);
  }

  SECTION("a file moved with its sidecar keeps its identity and its loaded object") {
    const auto to = assets / "models/moved wood.png";
    std::filesystem::rename(wood, to);
    std::filesystem::rename(wood.string() + ".meta", to.string() + ".meta");
    const auto result = database.rescan();
    REQUIRE(result.moved.size() == 1);
    CHECK(result.added.empty());
    CHECK(result.removed.empty());
    CHECK(result.moved[0].uuid == woodId);
    CHECK(result.moved[0].from == wood);
    CHECK(result.moved[0].to == to);
    CHECK(database.find(woodId)->source == to);
    CHECK(database.find(woodId)->name == "moved wood");
    CHECK(database.findByPath(to)->uuid == woodId);
    CHECK(database.texture(woodId) == texture);
    CHECK(database.pollChanges().empty());
  }

  SECTION("a model moved with its sidecar keeps its sub-assets") {
    const auto to = assets / "crate.gltf";
    const auto subAssets = database.assets().size();
    std::filesystem::rename(crate, to);
    std::filesystem::rename(crate.string() + ".meta", to.string() + ".meta");
    const auto result = database.rescan();
    REQUIRE(result.moved.size() == 1);
    CHECK(result.moved[0].type == AssetType::Model);
    CHECK(database.assets().size() == subAssets);
    for (const AssetInfo *info : database.assets()) {
      if (info->uuid == crateId || info->parent == crateId) {
        CHECK(info->source == to);
      }
    }
  }

  SECTION("a file moved without its sidecar is a new asset") {
    const auto to = assets / "models/moved wood.png";
    std::filesystem::rename(wood, to);
    std::filesystem::remove(wood.string() + ".meta");
    const auto result = database.rescan();
    CHECK(result.moved.empty());
    REQUIRE(result.removed.size() == 1);
    REQUIRE(result.added.size() == 1);
    CHECK(result.removed[0].uuid == woodId);
    CHECK(result.added[0].uuid != woodId);
    CHECK(database.find(woodId) == nullptr);
    CHECK(database.findByPath(to)->uuid == result.added[0].uuid);
  }
}
