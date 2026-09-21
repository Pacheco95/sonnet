#include "AssetTestSupport.h"

#include <sonnet/assets/AssetDatabase.h>
#include <sonnet/core/JobSystem.h>

#include <sonnet/platform/Platform.h>
#include <sonnet/renderer/Renderer.h>
#include <sonnet/rhi/NullDevice.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <format>
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
    REQUIRE(database.assets(AssetType::Mesh).size() == 6); // five built-ins and the crate
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
