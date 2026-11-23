// An instance overrides MeshRenderer::slotMaterials slot by slot (ADR-0045, issue #600): the patch
// form, the merge on load, the rules for nil, length and malformed keys, files written before it,
// a descendant of an instance, a cooked round trip and where Apply writes.
#include <sonnet/world/Scene.h>
#include <sonnet/world/World.h>

#include <sonnet/assets/Bundle.h>
#include <sonnet/core/Log.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/sinks/ostream_sink.h>

#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace sonnet;
using nlohmann::json;

namespace {

const core::Uuid Red = core::Uuid::parse("5a0c2f5e-0045-4a5b-8c9d-0000000000a1").value();
const core::Uuid Green = core::Uuid::parse("5a0c2f5e-0045-4a5b-8c9d-0000000000a2").value();
const core::Uuid Blue = core::Uuid::parse("5a0c2f5e-0045-4a5b-8c9d-0000000000a3").value();
const core::Uuid Grey = core::Uuid::parse("5a0c2f5e-0045-4a5b-8c9d-0000000000a4").value();
const std::string Nil = core::Uuid{}.toString();

using Slots = std::vector<core::Uuid>;

json list(const Slots &slots) {
  json out = json::array();
  for (const core::Uuid &slot : slots) {
    out.push_back(slot.toString());
  }
  return out;
}

json slotsOf(const Slots &slots) {
  return {{"slotMaterials", list(slots)}};
}

// A prefab "Figure" with a "Body" child, both holding a MeshRenderer with `slots`, as a file.
struct Figure {
  core::Uuid root = core::Uuid::generate();
  core::Uuid body = core::Uuid::generate();
  Slots rootSlots;
  Slots bodySlots;
  float rootRed = 0.5f; // a member besides the slots, which a prefab edit can change

  [[nodiscard]] json file() const {
    json rootComponents = slotsOf(rootSlots);
    rootComponents["color"] = json::array({rootRed, 0.0, 0.0, 1.0});
    return {{"version", world::SceneVersion},
            {"entities",
             {{{"uuid", root.toString()}, {"name", "Figure"}, {"components", {{"MeshRenderer", rootComponents}}}},
              {{"uuid", body.toString()},
               {"name", "Body"},
               {"parent", root.toString()},
               {"components", {{"MeshRenderer", slotsOf(bodySlots)}}}}}}};
  }

  void load(world::World &world) const {
    REQUIRE(world::loadPrefab(world, file()).has_value());
  }
};

const json &componentsOf(const json &scene, const core::Uuid &uuid) {
  for (const json &entry : scene["entities"]) {
    if (entry["uuid"] == uuid.toString()) {
      return entry["components"];
    }
  }
  FAIL("no entry for " << uuid.toString());
}

flecs::entity childNamed(const world::World &world, flecs::entity parent, std::string_view name) {
  for (const flecs::entity child : world.children(parent)) {
    if (child.get<world::Name>().value == name) {
      return child;
    }
  }
  return {};
}

Slots slotsOfEntity(flecs::entity entity) {
  return entity.get<world::MeshRenderer>().slotMaterials;
}

// The patch of `value` onto `source` and the merge of it back, for two slot lists.
json patchOf(const world::World &world, const Slots &value, const Slots &source) {
  return world::componentPatch(*world.findComponent("MeshRenderer"), slotsOf(value), slotsOf(source));
}

json mergedWith(const world::World &world, const Slots &target, const json &patch) {
  return world::mergePatch(*world.findComponent("MeshRenderer"), slotsOf(target), patch, "test");
}

} // namespace

TEST_CASE("a slot list patch holds only the slots that differ, by index", "[world][prefab][overrides][slots]") {
  const world::World world;
  // One slot differs.
  REQUIRE(patchOf(world, {Red, Green}, {Blue, Green}) == json{{"slotMaterials", {{"0", Red.toString()}}}});
  // Two do, with a slot between them that follows.
  REQUIRE(patchOf(world, {Red, Green, Blue}, {Grey, Green, Grey}) ==
          json{{"slotMaterials", {{"0", Red.toString()}, {"2", Blue.toString()}}}});
  // Equal lists, and lists equal once their trailing nils are dropped, write nothing.
  REQUIRE(patchOf(world, {Red, Green}, {Red, Green}).empty());
  REQUIRE(patchOf(world, {Red, {}}, {Red}).empty());
  REQUIRE(patchOf(world, {Red}, {Red, {}, {}}).empty());
  REQUIRE(patchOf(world, {}, {}).empty());
  // A shorter list stores a nil where the prefab has a material: the slot falls through.
  REQUIRE(patchOf(world, {Red}, {Red, Green}) == json{{"slotMaterials", {{"1", Nil}}}});
  REQUIRE(patchOf(world, {}, {Red, Green}) == json{{"slotMaterials", {{"0", Nil}, {"1", Nil}}}});
  // A longer one stores the keys past the prefab's end, and not the nil entries between.
  REQUIRE(patchOf(world, {Red, {}, Blue}, {Red}) == json{{"slotMaterials", {{"2", Blue.toString()}}}});
  REQUIRE(patchOf(world, {{}, Green}, {}) == json{{"slotMaterials", {{"1", Green.toString()}}}});
  // A list holds no other member, so another member that differs sits beside the slots.
  json value = slotsOf({Red, Green});
  value["visible"] = false;
  const json patch = world::componentPatch(*world.findComponent("MeshRenderer"), value, slotsOf({Red, Blue}));
  REQUIRE(patch == json{{"slotMaterials", {{"1", Green.toString()}}}, {"visible", false}});
}

TEST_CASE("an object of slots merges by position and an array replaces", "[world][prefab][overrides][slots]") {
  const world::World world;
  // The slots the object leaves out keep the prefab's.
  REQUIRE(mergedWith(world, {Red, Green, Blue}, {{"slotMaterials", {{"1", Grey.toString()}}}}) ==
          slotsOf({Red, Grey, Blue}));
  // The list grows with nil to the highest index named.
  REQUIRE(mergedWith(world, {Red}, {{"slotMaterials", {{"3", Blue.toString()}}}}) == slotsOf({Red, {}, {}, Blue}));
  // Nil is a value that is set; trailing nils are dropped after the merge.
  REQUIRE(mergedWith(world, {Red, Green}, {{"slotMaterials", {{"1", Nil}}}}) == slotsOf({Red}));
  REQUIRE(mergedWith(world, {Red, Green}, {{"slotMaterials", {{"0", Nil}}}}) == slotsOf({{}, Green}));
  REQUIRE(mergedWith(world, {Red, Green}, {{"slotMaterials", {{"0", Nil}, {"1", Nil}}}}) == slotsOf({}));
  // A file written before this decision holds an array, which replaces as it did.
  REQUIRE(mergedWith(world, {Red, Green}, slotsOf({Blue})) == slotsOf({Blue}));
  REQUIRE(mergedWith(world, {Red, Green}, slotsOf({})) == slotsOf({}));
  // A patch and the merge of it are inverse, whatever the lists.
  const core::Uuid none;
  const std::vector<std::pair<Slots, Slots>> cases = {
      {Slots{Red, Green}, Slots{Blue, Green}}, {Slots{Red}, Slots{Red, Green}}, {Slots{}, Slots{Red, Green}},
      {Slots{none, Green}, Slots{Red}},        {Slots{Red, none, Blue}, Slots{}}, {Slots{}, Slots{}},
  };
  for (const auto &[value, source] : cases) {
    REQUIRE(mergedWith(world, source, patchOf(world, value, source)) == slotsOf(value));
  }
}

TEST_CASE("a malformed key of a slot patch is skipped with a warning and the rest loads",
          "[world][prefab][overrides][slots]") {
  const world::World world;
  std::ostringstream log;
  const auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(log, true);
  core::Log::addSink(sink);
  const json patch = {{"slotMaterials",
                       {{"abc", Blue.toString()},
                        {"-1", Blue.toString()},
                        {"1.5", Blue.toString()},
                        {"", Blue.toString()},
                        {"1025", Blue.toString()},
                        {"2", "not a uuid"},
                        {"3", 7},
                        {"1", Green.toString()}}}};
  const json merged = mergedWith(world, {Red}, patch);
  core::Log::removeSink(sink);
  REQUIRE(merged == slotsOf({Red, Green}));
  const std::string text = log.str();
  for (const char *key : {"abc", "-1", "1.5", "", "1025", "2", "3"}) {
    INFO(key);
    CHECK(text.find(std::string{"entity test: "}) != std::string::npos);
    CHECK(text.find(std::string{"key \""} + key + "\"") != std::string::npos);
  }

  // The highest allowed index is 1024, and a member that is neither a list nor an object is skipped.
  const json high = mergedWith(world, {Red}, {{"slotMaterials", {{"1024", Blue.toString()}}}});
  REQUIRE(high["slotMaterials"].size() == 1025);
  REQUIRE(high["slotMaterials"][1024] == Blue.toString());
  REQUIRE(mergedWith(world, {Red}, {{"slotMaterials", 5}}) == slotsOf({Red}));
  REQUIRE(mergedWith(world, {Red}, {{"slotMaterials", "x"}}) == slotsOf({Red}));
}

TEST_CASE("an instance overrides one slot and follows the prefab for the others", "[world][scene][prefab][slots]") {
  Figure figure;
  figure.rootSlots = {Red, Green};
  json scene;
  core::Uuid instanceUuid;
  {
    world::World world;
    figure.load(world);
    const flecs::entity instance = world.instantiate(world.find(figure.root), "Figure 1");
    instanceUuid = world.uuidOf(instance);
    REQUIRE(slotsOfEntity(instance) == Slots{Red, Green});
    instance.ensure<world::MeshRenderer>().slotMaterials[0] = Blue;
    scene = world::saveScene(world);
  }
  // The saved slot alone: nothing else of the component, nothing for slot 1.
  REQUIRE(componentsOf(scene, instanceUuid)["MeshRenderer"] == json{{"slotMaterials", {{"0", Blue.toString()}}}});
  REQUIRE(scene["version"] == 10);

  // The prefab changes slot 1 and the colour: the instance keeps its slot 0 and takes both.
  figure.rootSlots = {Red, Grey};
  figure.rootRed = 0.9f;
  const auto cooked = assets::decodeJson(assets::encodeJson(scene));
  REQUIRE(cooked.has_value());
  for (const json &document : {scene, *cooked}) {
    world::World world;
    figure.load(world);
    REQUIRE(world::loadScene(world, document).has_value());
    const flecs::entity instance = world.find(instanceUuid);
    REQUIRE(slotsOfEntity(instance) == Slots{Blue, Grey});
    REQUIRE(instance.get<world::MeshRenderer>().color.r == 0.9f);
    REQUIRE(world::saveScene(world) == document); // and it saves the same again
  }

  // The prefab gains a slot: the instance follows it too.
  figure.rootSlots = {Red, Grey, Green};
  world::World world;
  figure.load(world);
  REQUIRE(world::loadScene(world, scene).has_value());
  REQUIRE(slotsOfEntity(world.find(instanceUuid)) == Slots{Blue, Grey, Green});
}

TEST_CASE("an instance's nil slot stays an override and its trailing nils do not count",
          "[world][scene][prefab][slots]") {
  Figure figure;
  figure.rootSlots = {Red, Green};
  json scene;
  core::Uuid instanceUuid;
  {
    world::World world;
    figure.load(world);
    const flecs::entity instance = world.instantiate(world.find(figure.root), "Figure 1");
    instanceUuid = world.uuidOf(instance);
    // Slot 1 falls through, so the list is shorter than the prefab's.
    instance.ensure<world::MeshRenderer>().slotMaterials = {Red};
    scene = world::saveScene(world);
  }
  REQUIRE(componentsOf(scene, instanceUuid)["MeshRenderer"] == json{{"slotMaterials", {{"1", Nil}}}});

  // The prefab later sets slot 1 to something else and adds a slot: the nil holds, the new slot follows.
  figure.rootSlots = {Red, Blue, Grey};
  world::World world;
  figure.load(world);
  REQUIRE(world::loadScene(world, scene).has_value());
  REQUIRE(slotsOfEntity(world.find(instanceUuid)) == Slots{Red, {}, Grey});

  // Clearing every slot stores a nil for each one the prefab has, and loads as the empty list.
  figure.rootSlots = {Red, Green};
  world::World clearing;
  figure.load(clearing);
  const flecs::entity cleared = clearing.instantiate(clearing.find(figure.root), "Figure 2");
  cleared.ensure<world::MeshRenderer>().slotMaterials.clear();
  const json clearedScene = world::saveScene(clearing);
  REQUIRE(componentsOf(clearedScene, clearing.uuidOf(cleared))["MeshRenderer"] ==
          json{{"slotMaterials", {{"0", Nil}, {"1", Nil}}}});
  world::World loaded;
  figure.load(loaded);
  REQUIRE(world::loadScene(loaded, clearedScene).has_value());
  REQUIRE(slotsOfEntity(loaded.find(clearing.uuidOf(cleared))).empty());

  // An instance that equals the prefab but for a trailing nil stores nothing.
  world::World equal;
  figure.load(equal);
  const flecs::entity same = equal.instantiate(equal.find(figure.root), "Figure 3");
  same.ensure<world::MeshRenderer>().slotMaterials = {Red, Green, {}};
  REQUIRE_FALSE(componentsOf(world::saveScene(equal), equal.uuidOf(same)).contains("MeshRenderer"));
}

TEST_CASE("a scene written before the object form loads as it did", "[world][scene][prefab][slots]") {
  Figure figure;
  figure.rootSlots = {Red, Green};
  const core::Uuid instanceUuid = core::Uuid::generate();
  const json entry = {{"uuid", instanceUuid.toString()},
                      {"name", "Figure 1"},
                      {"prefab", figure.root.toString()},
                      {"components", {{"MeshRenderer", slotsOf({Blue})}}}};
  for (const int version : {9, 10}) {
    world::World world;
    figure.load(world);
    REQUIRE(world::loadScene(world, {{"version", version}, {"entities", json::array({entry})}}).has_value());
    // The array replaces the prefab's list whole, in either version.
    REQUIRE(slotsOfEntity(world.find(instanceUuid)) == Slots{Blue});
    // A save writes the object form.
    REQUIRE(componentsOf(world::saveScene(world), instanceUuid)["MeshRenderer"] ==
            json{{"slotMaterials", {{"0", Blue.toString()}, {"1", Nil}}}});
  }
}

TEST_CASE("a descendant of an instance overrides one slot", "[world][scene][prefab][slots]") {
  Figure figure;
  figure.bodySlots = {Red, Green, Blue};
  json scene;
  core::Uuid instanceUuid;
  core::Uuid bodyUuid;
  {
    world::World world;
    figure.load(world);
    const flecs::entity instance = world.instantiate(world.find(figure.root), "Figure 1");
    instanceUuid = world.uuidOf(instance);
    const flecs::entity body = childNamed(world, instance, "Body");
    bodyUuid = world.uuidOf(body);
    body.ensure<world::MeshRenderer>().slotMaterials[1] = Grey;
    scene = world::saveScene(world);
  }
  const json &stored = componentsOf(scene, bodyUuid);
  REQUIRE(stored["MeshRenderer"] == json{{"slotMaterials", {{"1", Grey.toString()}}}});

  // The prefab changes the other slots: the descendant follows them.
  figure.bodySlots = {Blue, Green, Red, Red};
  world::World world;
  figure.load(world);
  REQUIRE(world::loadScene(world, scene).has_value());
  REQUIRE(slotsOfEntity(childNamed(world, world.find(instanceUuid), "Body")) == Slots{Blue, Grey, Red, Red});
}

TEST_CASE("Apply compares an indexed list through its padding", "[world][prefab][nested][apply][slots]") {
  // A wheel prefab whose hub holds no slots, inside a cart prefab whose copy of the hub sets slot 1.
  const core::Uuid wheel = core::Uuid::generate();
  const core::Uuid hub = core::Uuid::generate();
  const core::Uuid cart = core::Uuid::generate();
  const core::Uuid axle = core::Uuid::generate();
  const json wheelFile = {{"version", world::SceneVersion},
                          {"entities",
                           {{{"uuid", wheel.toString()}, {"name", "Wheel"}, {"components", json::object()}},
                            {{"uuid", hub.toString()},
                             {"name", "Hub"},
                             {"parent", wheel.toString()},
                             {"components", {{"MeshRenderer", {{"visible", true}}}}}}}}};
  const json cartFile = {{"version", world::SceneVersion},
                         {"entities",
                          {{{"uuid", cart.toString()}, {"name", "Cart"}, {"components", json::object()}},
                           {{"uuid", axle.toString()},
                            {"name", "Axle"},
                            {"parent", cart.toString()},
                            {"prefab", wheel.toString()},
                            {"components", json::object()}}}}};
  world::World world;
  REQUIRE(world::loadPrefabs(world, {{"cart.prefab.json", cartFile, {}}, {"wheel.prefab.json", wheelFile, {}}}).empty());
  const flecs::entity wheelPrefab = world.find(wheel);
  const flecs::entity cartPrefab = world.find(cart);
  wheelPrefab.set<world::PrefabFile>({"wheel.prefab.json"});
  cartPrefab.set<world::PrefabFile>({"cart.prefab.json"});
  const flecs::entity wheelHub = childNamed(world, wheelPrefab, "Hub");
  const flecs::entity cartHub = childNamed(world, childNamed(world, cartPrefab, "Axle"), "Hub");
  const world::ComponentInfo &meshRenderer = *world.findComponent("MeshRenderer");
  const flecs::entity instance = world.instantiate(cartPrefab, "Cart 1");
  const flecs::entity nestedHub = childNamed(world, childNamed(world, instance, "Axle"), "Hub");

  const auto entities = [](const std::vector<world::ApplyTarget> &targets) {
    std::vector<flecs::entity> out;
    for (const world::ApplyTarget &target : targets) {
      out.push_back(target.entity);
    }
    return out;
  };
  using Entities = std::vector<flecs::entity>;
  const auto at = [](int slot) { return json::json_pointer{"/slotMaterials/" + std::to_string(slot)}; };

  // The cart sets slot 1 only; its slot 0 is a nil that the wheel's absent slot equals, so slot 0
  // goes as deep as the wheel, and slot 1 stops at the cart.
  cartHub.ensure<world::MeshRenderer>().slotMaterials = {{}, Green};
  CHECK(entities(world.applyTargets(nestedHub, meshRenderer, at(0))) == Entities{wheelHub, cartHub});
  CHECK(entities(world.applyTargets(nestedHub, meshRenderer, at(1))) == Entities{cartHub});
  // An index past both lists is the same slot at every level.
  CHECK(entities(world.applyTargets(nestedHub, meshRenderer, at(5))) == Entities{wheelHub, cartHub});
  // The whole list differs at the cart's level.
  CHECK(entities(world.applyTargets(nestedHub, meshRenderer, json::json_pointer{"/slotMaterials"})) ==
        Entities{cartHub});

  // A list that differs from the wheel's only by trailing nils is the same list.
  cartHub.ensure<world::MeshRenderer>().slotMaterials = {{}, {}};
  CHECK(entities(world.applyTargets(nestedHub, meshRenderer, json::json_pointer{"/slotMaterials"})) ==
        Entities{wheelHub, cartHub});
}
