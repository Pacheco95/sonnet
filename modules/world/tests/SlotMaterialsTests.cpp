// MeshRenderer::slotMaterials (ADR-0041, "Per-submesh slots"; issue #542): the order the draw list
// resolves a submesh's material in, the warning for a slot the mesh lacks, and the field through
// scene, prefab and bundle files.

#include <sonnet/world/DrawList.h>
#include <sonnet/world/Scene.h>
#include <sonnet/world/World.h>

#include <sonnet/assets/AssetDatabase.h>
#include <sonnet/assets/Bundle.h>
#include <sonnet/assets/Cook.h>
#include <sonnet/assets/Project.h>
#include <sonnet/core/File.h>
#include <sonnet/core/JobSystem.h>
#include <sonnet/core/Log.h>
#include <sonnet/platform/Platform.h>
#include <sonnet/renderer/Renderer.h>
#include <sonnet/rhi/NullDevice.h>

#include <TestSupport.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/sinks/ostream_sink.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace sonnet;

namespace {

// One mesh of two primitives sharing a triangle, the first with the material "Body", the second
// with "Skin": two submeshes in two slots.
constexpr const char *TwoSlotModel =
    R"json({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"name":"Figure","mesh":0}],)json"
    R"json("meshes":[{"name":"FigureMesh","primitives":[{"attributes":{"POSITION":0},"indices":1,"material":0},)json"
    R"json({"attributes":{"POSITION":0},"indices":1,"material":1}]}],"materials":[{"name":"Body"},{"name":"Skin"}],)json"
    R"json("buffers":[{"uri":"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAEAAAACAAAA","byteLength":48}],)json"
    R"json("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":12}],)json"
    R"json("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},)json"
    R"json({"bufferView":1,"componentType":5125,"count":3,"type":"SCALAR"}]})json";

// A project with the two-slot model and three material files, open in a database.
struct Fixture {
  platform::Platform platform{{.headless = true}};
  std::unique_ptr<rhi::NullDevice> device = rhi::createNullDevice();
  renderer::Renderer renderer{*device, platform.basePath() / "shaders"};
  core::JobSystem jobs{{.workerCount = 2}};
  assets::AssetDatabase assets{renderer, jobs};
  std::filesystem::path root;
  core::Uuid model;
  core::Uuid mesh;
  core::Uuid body; // the glTF's materials
  core::Uuid skin;
  core::Uuid red; // material files
  core::Uuid green;
  core::Uuid blue;

  explicit Fixture(std::string_view name) : root(std::filesystem::temp_directory_path() / name) {
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "assets");
    std::filesystem::create_directories(root / "scenes");
    REQUIRE(core::writeFile(root / "assets" / "figure.gltf",
                            std::as_bytes(std::span{TwoSlotModel, std::strlen(TwoSlotModel)}))
                .has_value());
    assets.open(root, std::vector<std::string>{"assets"});
    const assets::AssetInfo *info = assets.findByPath(root / "assets" / "figure.gltf");
    REQUIRE(info != nullptr);
    model = info->uuid;
    for (const assets::AssetInfo *sub : assets.assets()) {
      if (sub->parent == model && sub->type == assets::AssetType::Mesh) {
        mesh = sub->uuid;
      }
    }
    REQUIRE_FALSE(mesh.isNil());
    const assets::AssetInfo *meshInfo = assets.find(mesh);
    REQUIRE(meshInfo->materials.size() == 2);
    body = meshInfo->materials[0];
    skin = meshInfo->materials[1];
    REQUIRE(assets.find(body)->name == "Body");
    REQUIRE(assets.find(skin)->name == "Skin");
    red = created("red", {1.0f, 0.0f, 0.0f, 1.0f});
    green = created("green", {0.0f, 1.0f, 0.0f, 1.0f});
    blue = created("blue", {0.0f, 0.0f, 1.0f, 1.0f});
  }
  ~Fixture() {
    assets.close();
    sonnet::test::removeScratch(root);
  }
  Fixture(const Fixture &) = delete;
  Fixture &operator=(const Fixture &) = delete;

  core::Uuid created(const char *name, glm::vec4 colour) {
    assets::MaterialSource source;
    source.baseColor = colour;
    const auto uuid = assets.createMaterial(root / "assets" / (std::string{name} + ".material.json"), source);
    REQUIRE(uuid.has_value());
    return *uuid;
  }

  // The material each slot of `entity`'s mesh draws with, by slot.
  std::vector<renderer::MaterialHandle> drawn(world::World &world, flecs::entity entity) {
    world.progress(0.0f);
    std::vector<renderer::DrawItem> draws;
    std::vector<glm::mat4> joints;
    std::vector<float> morphWeights;
    // Requested on the job system: wait for the mesh, then build the list it is in.
    world::buildDrawList(world, assets, draws, joints, morphWeights);
    assets.waitForLoads();
    world::buildDrawList(world, assets, draws, joints, morphWeights);
    const renderer::MeshHandle handle = assets.mesh(entity.get<world::MeshRenderer>().mesh);
    const std::span<const renderer::Submesh> submeshes = renderer.submeshes(handle);
    std::vector<renderer::MaterialHandle> bySlot(submeshes.size());
    for (const renderer::DrawItem &draw : draws) {
      if (draw.id == world::World::pickId(entity)) {
        bySlot.at(submeshes[draw.submesh].materialSlot) = draw.material;
      }
    }
    return bySlot;
  }
};

} // namespace

TEST_CASE("a submesh draws with its slot's material, then the renderer's, then the mesh's own",
          "[world][drawlist][slots]") {
  Fixture fixture{"sonnet_world_slot_order"};
  world::World world;
  assets::AssetDatabase &assets = fixture.assets;
  const flecs::entity figure = world.createEntity("Figure");
  figure.set<world::MeshRenderer>({.mesh = fixture.mesh});
  REQUIRE(world::materialSlotCount(assets, fixture.mesh) == 2);

  // No override: each slot has the glTF's own material.
  REQUIRE(fixture.drawn(world, figure) == std::vector{assets.material(fixture.body), assets.material(fixture.skin)});

  // `material` overrides every slot, as before slots existed.
  figure.ensure<world::MeshRenderer>().material = fixture.red;
  REQUIRE(fixture.drawn(world, figure) == std::vector{assets.material(fixture.red), assets.material(fixture.red)});

  // A slot's entry wins over it; a nil entry falls through to it.
  figure.ensure<world::MeshRenderer>().slotMaterials = {core::Uuid{}, fixture.green};
  REQUIRE(fixture.drawn(world, figure) == std::vector{assets.material(fixture.red), assets.material(fixture.green)});

  // Without `material`, a nil entry falls through to the mesh's own.
  figure.ensure<world::MeshRenderer>().material = {};
  REQUIRE(fixture.drawn(world, figure) == std::vector{assets.material(fixture.body), assets.material(fixture.green)});

  // Both slots overridden; a material that does not resolve falls through like a nil one.
  figure.ensure<world::MeshRenderer>().slotMaterials = {fixture.blue, core::Uuid::generate()};
  REQUIRE(fixture.drawn(world, figure) == std::vector{assets.material(fixture.blue), assets.material(fixture.skin)});

  // A built-in mesh has no default material but one slot, which an entry overrides.
  const flecs::entity box = world.createEntity("Box");
  box.set<world::MeshRenderer>({.mesh = assets::builtin::box(), .slotMaterials = {fixture.green}});
  REQUIRE(world::materialSlotCount(assets, assets::builtin::box()) == 1);
  REQUIRE(fixture.drawn(world, box) == std::vector{assets.material(fixture.green)});
}

TEST_CASE("a slot material past the mesh's slots is ignored with one warning", "[world][drawlist][slots]") {
  Fixture fixture{"sonnet_world_slot_past"};
  world::World world;
  const flecs::entity figure = world.createEntity("Figure");
  figure.set<world::MeshRenderer>({.mesh = fixture.mesh, .slotMaterials = {fixture.red, {}, fixture.blue}});
  const flecs::entity quiet = world.createEntity("Quiet");
  // Nil entries past the end ask for nothing, so they are not warned about.
  quiet.set<world::MeshRenderer>({.mesh = fixture.mesh, .slotMaterials = {{}, {}, {}, {}}});

  std::ostringstream log;
  const auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(log, true);
  core::Log::addSink(sink);
  for (int frame = 0; frame < 3; ++frame) {
    REQUIRE(fixture.drawn(world, figure) ==
            std::vector{fixture.assets.material(fixture.red), fixture.assets.material(fixture.skin)});
  }
  core::Log::removeSink(sink);
  const std::string text = log.str();
  const std::string warning = "Figure: slot material 2 is past the 2 slots of mesh";
  const auto at = text.find(warning);
  REQUIRE(at != std::string::npos);
  REQUIRE(text.find(warning, at + 1) == std::string::npos); // once, not every frame
  REQUIRE(text.find("Quiet: slot material") == std::string::npos);
}

TEST_CASE("slot materials are left out of a file while empty and round-trip through it and a bundle",
          "[world][scene][slots]") {
  Fixture fixture{"sonnet_world_slot_scene"};
  world::World world;
  const flecs::entity plain = world.createEntity("Plain");
  plain.set<world::MeshRenderer>({.mesh = fixture.mesh, .material = fixture.red});
  const flecs::entity figure = world.createEntity("Figure");
  figure.set<world::MeshRenderer>({.mesh = fixture.mesh, .slotMaterials = {core::Uuid{}, fixture.green}});
  const core::Uuid plainUuid = world.uuidOf(plain);
  const core::Uuid figureUuid = world.uuidOf(figure);
  const flecs::entity_t meshRenderer = world.findComponent("MeshRenderer")->id;
  // Undo copies and the recovery file keep the member even when empty, so restoring a value
  // clears slots that were set after it.
  REQUIRE(world.componentToJson(plain, meshRenderer).at("slotMaterials") == nlohmann::json::array());

  const nlohmann::json saved = world::saveScene(world);
  REQUIRE(saved["version"] == 9); // an additive member: the version stays (ADR-0029)
  const auto entry = [&](const nlohmann::json &document, const core::Uuid &uuid) {
    const auto found = std::ranges::find_if(
        document["entities"], [&](const nlohmann::json &entity) { return entity["uuid"] == uuid.toString(); });
    REQUIRE(found != document["entities"].end());
    return (*found)["components"]["MeshRenderer"];
  };
  REQUIRE_FALSE(entry(saved, plainUuid).contains("slotMaterials"));
  REQUIRE(entry(saved, figureUuid)["slotMaterials"] ==
          nlohmann::json::array({core::Uuid{}.toString(), fixture.green.toString()}));

  // A scene saved before the member existed is the same document as one saved now without slots.
  nlohmann::json old = saved;
  for (nlohmann::json &entity : old["entities"]) {
    entity["components"]["MeshRenderer"].erase("slotMaterials");
  }
  {
    world::World loaded;
    REQUIRE(world::loadScene(loaded, old).has_value());
    REQUIRE(loaded.find(plainUuid).get<world::MeshRenderer>().slotMaterials.empty());
    REQUIRE(loaded.find(plainUuid).get<world::MeshRenderer>().material == fixture.red);
    REQUIRE(world::saveScene(loaded) == old);
  }

  // Through a real cook: the scene file and its materials into a bundle, read back by a player's
  // database, draws as the source does.
  REQUIRE(world::saveSceneFile(world, fixture.root / "scenes" / "main.scene.json").has_value());
  assets::Project project;
  project.root = fixture.root;
  project.name = "Slots";
  project.assetRoots = {"assets"};
  REQUIRE(project.save().has_value());
  const std::filesystem::path out = fixture.root / "export";
  REQUIRE(assets::cook(fixture.assets, project, {.outputDirectory = out}).has_value());
  const std::vector<renderer::MaterialHandle> source = fixture.drawn(world, figure);
  REQUIRE(source == std::vector{fixture.assets.material(fixture.body), fixture.assets.material(fixture.green)});

  assets::AssetDatabase player{fixture.renderer, fixture.jobs};
  REQUIRE(player.openBundle(out / "game.sbundle").has_value());
  const auto bytes = player.bundle()->read("scenes/main.scene.json");
  REQUIRE(bytes.has_value());
  const auto cooked = assets::decodeJson(*bytes);
  REQUIRE(cooked.has_value());
  REQUIRE(*cooked == saved);
  world::World playing;
  REQUIRE(world::loadScene(playing, *cooked).has_value());
  const flecs::entity cookedFigure = playing.find(figureUuid);
  REQUIRE(cookedFigure.get<world::MeshRenderer>().slotMaterials == figure.get<world::MeshRenderer>().slotMaterials);
  playing.progress(0.0f);
  std::vector<renderer::DrawItem> draws;
  std::vector<glm::mat4> joints;
  std::vector<float> morphWeights;
  world::buildDrawList(playing, player, draws, joints, morphWeights);
  player.waitForLoads();
  world::buildDrawList(playing, player, draws, joints, morphWeights);
  std::vector<core::Uuid> materials; // by slot, compared by asset since the handles are the player's
  const std::span<const renderer::Submesh> submeshes = fixture.renderer.submeshes(player.mesh(fixture.mesh));
  materials.resize(submeshes.size());
  for (const renderer::DrawItem &draw : draws) {
    if (draw.id == world::World::pickId(cookedFigure)) {
      const std::uint32_t slot = submeshes[draw.submesh].materialSlot;
      for (const core::Uuid candidate : {fixture.body, fixture.skin, fixture.green}) {
        if (draw.material == player.material(candidate)) {
          materials.at(slot) = candidate;
        }
      }
    }
  }
  REQUIRE(materials == std::vector{fixture.body, fixture.green});
  player.close();
}

TEST_CASE("a prefab instance overrides one slot and follows the prefab's other changes",
          "[world][scene][prefab][slots]") {
  Fixture fixture{"sonnet_world_slot_prefab"};
  nlohmann::json prefabFile;
  core::Uuid prefabRoot;
  {
    world::World authoring;
    const flecs::entity root = authoring.createEntity("Figure");
    root.set<world::MeshRenderer>({.mesh = fixture.mesh});
    prefabRoot = authoring.uuidOf(root);
    prefabFile = world::savePrefab(authoring, root);
  }
  REQUIRE_FALSE(prefabFile["entities"][0]["components"]["MeshRenderer"].contains("slotMaterials"));

  core::Uuid instanceUuid;
  nlohmann::json scene;
  {
    world::World editing;
    REQUIRE(world::loadPrefab(editing, prefabFile).has_value());
    const flecs::entity instance = editing.instantiate(editing.find(prefabRoot), "Figure 1");
    instanceUuid = editing.uuidOf(instance);
    instance.ensure<world::MeshRenderer>().slotMaterials = {core::Uuid{}, fixture.green};
    scene = world::saveScene(editing);
  }
  const auto instanceEntry = std::ranges::find_if(
      scene["entities"], [&](const nlohmann::json &entity) { return entity["uuid"] == instanceUuid.toString(); });
  REQUIRE(instanceEntry != scene["entities"].end());
  // The slot list alone, written whole as any list is (ADR-0037).
  REQUIRE((*instanceEntry)["components"]["MeshRenderer"] ==
          nlohmann::json{{"slotMaterials", {core::Uuid{}.toString(), fixture.green.toString()}}});

  // The prefab later overrides every slot and recolours: the instance keeps its slot and takes the rest.
  prefabFile["entities"][0]["components"]["MeshRenderer"]["material"] = fixture.red.toString();
  prefabFile["entities"][0]["components"]["MeshRenderer"]["color"] = {0.1, 0.2, 0.3, 1.0};
  const auto cooked = assets::decodeJson(assets::encodeJson(scene));
  REQUIRE(cooked.has_value());
  for (const nlohmann::json &document : {scene, *cooked}) {
    world::World playing;
    REQUIRE(world::loadPrefab(playing, prefabFile).has_value());
    REQUIRE(world::loadScene(playing, document).has_value());
    const flecs::entity instance = playing.find(instanceUuid);
    const world::MeshRenderer &loaded = instance.get<world::MeshRenderer>();
    REQUIRE(loaded.slotMaterials == std::vector{core::Uuid{}, fixture.green});
    REQUIRE(loaded.material == fixture.red);
    REQUIRE(loaded.color.r == 0.1f);
    REQUIRE(fixture.drawn(playing, instance) ==
            std::vector{fixture.assets.material(fixture.red), fixture.assets.material(fixture.green)});
  }

  // A prefab with slots, and an instance that clears them: the empty list is the instance's value.
  prefabFile["entities"][0]["components"]["MeshRenderer"]["slotMaterials"] = {fixture.blue.toString()};
  {
    world::World editing;
    REQUIRE(world::loadPrefab(editing, prefabFile).has_value());
    const flecs::entity instance = editing.instantiate(editing.find(prefabRoot), "Figure 2", {}, instanceUuid);
    REQUIRE(instance.get<world::MeshRenderer>().slotMaterials == std::vector{fixture.blue});
    instance.ensure<world::MeshRenderer>().slotMaterials.clear();
    scene = world::saveScene(editing);
  }
  REQUIRE(scene["entities"][0]["components"]["MeshRenderer"] ==
          nlohmann::json{{"slotMaterials", nlohmann::json::array()}});
  world::World playing;
  REQUIRE(world::loadPrefab(playing, prefabFile).has_value());
  REQUIRE(world::loadScene(playing, scene).has_value());
  REQUIRE(playing.find(instanceUuid).get<world::MeshRenderer>().slotMaterials.empty());
}
