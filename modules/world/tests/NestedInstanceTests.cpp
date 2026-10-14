// Nested scene instances (ADR-0028): prefabs of prefabs, the identities derived for an instance's
// descendants, what a scene stores of them, and prefab documents loaded in dependency order.
#include <sonnet/world/Scene.h>
#include <sonnet/world/World.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>
#include <vector>

using namespace sonnet;
using Catch::Approx;
using nlohmann::json;

namespace {

// Wheel: a root with a "Hub" child. Cart: a root with a plain "Body" and an "Axle" that is an
// instance of Wheel. Both as their files would be written, so a test can change one and load
// again.
struct Prefabs {
  core::Uuid wheel = core::Uuid::generate();
  core::Uuid hub = core::Uuid::generate();
  core::Uuid cart = core::Uuid::generate();
  core::Uuid body = core::Uuid::generate();
  core::Uuid axle = core::Uuid::generate();
  float hubGrey = 0.5f;
  float hubHeight = 0.0f;
  bool bodySpins = false;
  bool withHub = true;

  json wheelFile() const {
    json entities = {{{"uuid", wheel.toString()}, {"name", "Wheel"}, {"components", json::object()}}};
    if (withHub) {
      entities.push_back({{"uuid", hub.toString()},
                          {"name", "Hub"},
                          {"parent", wheel.toString()},
                          {"components",
                           {{"MeshRenderer", {{"color", {{"x", hubGrey}, {"y", hubGrey}, {"z", hubGrey}, {"w", 1.0}}}}},
                            {"Transform", {{"position", {{"x", 0.0}, {"y", hubHeight}, {"z", 0.0}}}}}}}});
    }
    return {{"version", world::SceneVersion}, {"entities", entities}};
  }

  json cartFile() const {
    json bodyComponents = {{"Static", nullptr}};
    if (bodySpins) {
      bodyComponents["Spin"] = {{"speed", 3.0}};
    }
    return {
        {"version", world::SceneVersion},
        {"entities",
         {{{"uuid", cart.toString()}, {"name", "Cart"}, {"components", json::object()}},
          {{"uuid", body.toString()}, {"name", "Body"}, {"parent", cart.toString()}, {"components", bodyComponents}},
          {{"uuid", axle.toString()},
           {"name", "Axle"},
           {"parent", cart.toString()},
           {"prefab", wheel.toString()},
           {"components", json::object()}}}}};
  }

  // The outer prefab first, as a file listing may give it.
  void load(world::World &world) const {
    const std::vector<core::Error> errors =
        world::loadPrefabs(world, {{"cart.prefab.json", cartFile(), {}}, {"wheel.prefab.json", wheelFile(), {}}});
    REQUIRE(errors.empty());
  }
};

flecs::entity childNamed(const world::World &world, flecs::entity parent, std::string_view name) {
  for (const flecs::entity child : world.children(parent)) {
    if (child.get<world::Name>().value == name) {
      return child;
    }
  }
  return {};
}

// Every identity under `root`, depth first.
void collect(const world::World &world, flecs::entity root, std::vector<core::Uuid> &out) {
  out.push_back(world.uuidOf(root));
  for (const flecs::entity child : world.children(root)) {
    collect(world, child, out);
  }
}

} // namespace

TEST_CASE("a prefab of prefabs loads in any order and instantiates to every depth", "[world][prefab][nested]") {
  world::World world;
  const Prefabs files;
  files.load(world);
  REQUIRE(world.prefabs().size() == 2);

  // Inside the outer prefab, the inner instance's copied child is a prefab entity too, so no
  // query sees it as part of the scene.
  const flecs::entity cartPrefab = world.find(files.cart);
  const flecs::entity prefabHub = childNamed(world, childNamed(world, cartPrefab, "Axle"), "Hub");
  REQUIRE(prefabHub.has(flecs::Prefab));
  REQUIRE(world.roots().empty());

  const flecs::entity cart = world.instantiate(cartPrefab, "Cart 1");
  const flecs::entity axle = childNamed(world, cart, "Axle");
  const flecs::entity hub = childNamed(world, axle, "Hub");
  REQUIRE(hub);
  REQUIRE(!hub.has(flecs::Prefab));
  // The nested instance keeps its IsA; every descendant knows its source and its instance.
  REQUIRE(world.prefabOf(axle) == world.find(files.wheel));
  REQUIRE(axle.get<world::PrefabSource>().uuid == files.axle);
  REQUIRE(hub.get<world::PrefabSource>().uuid == world.uuidOf(prefabHub));
  REQUIRE(world.instanceOf(hub) == cart);
  REQUIRE(world.instanceOf(axle) == cart);
  REQUIRE(!world.instanceOf(cart));
  REQUIRE(world.uuidOf(hub) == world::World::instancedIdentity(world.uuidOf(cart), world.uuidOf(prefabHub)));
  REQUIRE(!cart.has<world::PrefabOrigin>());
  REQUIRE(!hub.has<world::PrefabOrigin>());
  REQUIRE(hub.get<world::MeshRenderer>().color.r == Approx(0.5f));
}

TEST_CASE("derived identities are the same on every load and distinct per instance", "[world][prefab][nested]") {
  const Prefabs files;
  const core::Uuid first = core::Uuid::generate();
  const core::Uuid second = core::Uuid::generate();
  std::vector<core::Uuid> a;
  std::vector<core::Uuid> b;
  std::vector<core::Uuid> c;
  {
    world::World world;
    files.load(world);
    collect(world, world.instantiate(world.find(files.cart), "Cart 1", {}, first), a);
    collect(world, world.instantiate(world.find(files.cart), "Cart 2", {}, second), c);
  }
  {
    world::World world;
    files.load(world);
    collect(world, world.instantiate(world.find(files.cart), "Cart 1", {}, first), b);
  }
  REQUIRE(a.size() == 4); // Cart, Body, Axle, Hub
  REQUIRE(a == b);
  std::set<core::Uuid> all(a.begin(), a.end());
  all.insert(c.begin(), c.end());
  REQUIRE(all.size() == 8);
}

TEST_CASE("a scene keeps the changes inside an instance and takes the prefab's other changes",
          "[world][scene][nested]") {
  Prefabs files;
  json scene;
  core::Uuid cartUuid;
  core::Uuid hubUuid;
  {
    world::World world;
    files.load(world);
    const flecs::entity cart = world.instantiate(world.find(files.cart), "Cart 1");
    cartUuid = world.uuidOf(cart);
    // Unchanged, an instance is one entry.
    REQUIRE(world::saveScene(world)["entities"].size() == 1);

    const flecs::entity body = childNamed(world, cart, "Body");
    const flecs::entity axle = childNamed(world, cart, "Axle");
    const flecs::entity hub = childNamed(world, axle, "Hub");
    hubUuid = world.uuidOf(hub);
    hub.ensure<world::MeshRenderer>().color.r = 1.0f; // a change two levels down
    body.add<world::Disabled>();                      // a source child hidden
    body.remove<world::Static>();                     // a source component removed
    axle.set<world::Name>({"Front axle"});            // a rename
    world.createEntity("Lamp", body);                 // a child added under an instance's child

    scene = world::saveScene(world);
    INFO(scene.dump(2));
    // The instance, three instanced entries and the added child.
    const json &entities = scene["entities"];
    REQUIRE(entities.size() == 5);
    for (const json &entry : entities) {
      if (entry.value("instanced", false)) {
        REQUIRE(!entry.contains("parent"));
        REQUIRE(!entry.contains("prefab"));
      }
      if (entry["uuid"] == world.uuidOf(body).toString()) {
        REQUIRE(entry["components"].contains("Disabled"));
        REQUIRE(entry["removed"] == json::array({"Static"}));
      }
      if (entry["uuid"] == hubUuid.toString()) {
        REQUIRE(entry["components"].size() == 1);
        REQUIRE(entry["components"].contains("MeshRenderer"));
      }
    }
  }
  // The prefabs change after the scene was saved.
  files.hubGrey = 0.8f;
  files.hubHeight = 2.0f;
  files.bodySpins = true;
  {
    world::World world;
    files.load(world);
    REQUIRE(world::loadScene(world, scene).has_value());
    const flecs::entity cart = world.find(cartUuid);
    REQUIRE(cart);
    const flecs::entity body = childNamed(world, cart, "Body");
    const flecs::entity axle = childNamed(world, cart, "Front axle");
    REQUIRE(axle);
    const flecs::entity hub = world.find(hubUuid);
    REQUIRE(hub.parent() == axle);
    // The changed component is the scene's, whole: red from the scene, green as it was saved,
    // not the prefab's new grey.
    REQUIRE(hub.get<world::MeshRenderer>().color.r == Approx(1.0f));
    REQUIRE(hub.get<world::MeshRenderer>().color.g == Approx(0.5f));
    // A component the instance did not change follows the prefab.
    REQUIRE(hub.get<world::Transform>().position.y == Approx(2.0f));
    REQUIRE(body.has<world::Disabled>());
    REQUIRE(!body.has<world::Static>());
    REQUIRE(body.has<world::Spin>()); // added to the prefab later, reaches the instance
    REQUIRE(childNamed(world, body, "Lamp"));
    // Saving again writes the same document.
    REQUIRE(world::saveScene(world) == scene);
  }
}

TEST_CASE("a change inside a prefab of prefabs reaches that prefab's instances", "[world][prefab][nested]") {
  const Prefabs files;
  json cartFile;
  core::Uuid cartUuid;
  {
    // Authoring the outer prefab as the editor would: scene entities, then savePrefab.
    world::World world;
    REQUIRE(world::loadPrefab(world, files.wheelFile()).has_value());
    const flecs::entity cart = world.createEntity("Cart");
    cartUuid = world.uuidOf(cart);
    const flecs::entity axle = world.instantiate(world.find(files.wheel), "Axle", cart);
    childNamed(world, axle, "Hub").ensure<world::MeshRenderer>().color.r = 1.0f;
    cartFile = world::savePrefab(world, cart);
    INFO(cartFile.dump(2));
    REQUIRE(cartFile["entities"].size() == 3); // Cart, Axle, the Hub's change
  }
  world::World world;
  REQUIRE(world::loadPrefabs(world, {{"cart", cartFile, {}}, {"wheel", files.wheelFile(), {}}}).empty());
  const flecs::entity cart = world.instantiate(world.find(cartUuid), "Cart 1");
  const flecs::entity hub = childNamed(world, childNamed(world, cart, "Axle"), "Hub");
  REQUIRE(hub.get<world::MeshRenderer>().color.r == Approx(1.0f));
}

TEST_CASE("a deleted instance comes back with its descendants' identities and changes", "[world][scene][nested]") {
  const Prefabs files;
  world::World world;
  files.load(world);
  const flecs::entity cart = world.instantiate(world.find(files.cart), "Cart 1");
  const flecs::entity hub = childNamed(world, childNamed(world, cart, "Axle"), "Hub");
  hub.ensure<world::MeshRenderer>().color.r = 1.0f;
  world.createEntity("Lamp", childNamed(world, cart, "Body"));
  const core::Uuid hubUuid = world.uuidOf(hub);

  // What the editor's delete command keeps, and what its undo loads back.
  const json kept = world::saveSubtree(world, cart);
  world.destroyEntity(cart);
  REQUIRE(!world.find(hubUuid));
  REQUIRE(world::loadScene(world, kept).has_value());
  const flecs::entity restored = world.find(hubUuid);
  REQUIRE(restored);
  REQUIRE(restored.get<world::MeshRenderer>().color.r == Approx(1.0f));
  REQUIRE(childNamed(world, childNamed(world, world.roots().front(), "Body"), "Lamp"));
}

TEST_CASE("a change whose entity left the prefab is skipped", "[world][scene][nested]") {
  Prefabs files;
  json scene;
  {
    world::World world;
    files.load(world);
    const flecs::entity cart = world.instantiate(world.find(files.cart), "Cart 1");
    childNamed(world, childNamed(world, cart, "Axle"), "Hub").ensure<world::MeshRenderer>().color.r = 1.0f;
    scene = world::saveScene(world);
  }
  files.withHub = false;
  world::World world;
  files.load(world);
  REQUIRE(world::loadScene(world, scene).has_value());
  REQUIRE(world.roots().size() == 1);
  REQUIRE(world::saveScene(world)["entities"].size() == 1); // and is gone on the next save
}

TEST_CASE("a prefab that contains itself is refused with the chain", "[world][prefab][nested]") {
  const core::Uuid a = core::Uuid::generate();
  const core::Uuid b = core::Uuid::generate();
  const core::Uuid plain = core::Uuid::generate();
  const auto file = [](const core::Uuid &root, const core::Uuid &uses) {
    json entities = {{{"uuid", root.toString()}, {"name", "Root"}, {"components", json::object()}}};
    if (!uses.isNil()) {
      entities.push_back({{"uuid", core::Uuid::generate().toString()},
                          {"name", "Inner"},
                          {"parent", root.toString()},
                          {"prefab", uses.toString()},
                          {"components", json::object()}});
    }
    return json{{"version", world::SceneVersion}, {"entities", entities}};
  };
  world::World world;
  const std::vector<core::Error> errors = world::loadPrefabs(
      world, {{"a.prefab.json", file(a, b), {}}, {"b.prefab.json", file(b, a), {}}, {"plain", file(plain, {}), {}}});
  REQUIRE(errors.size() == 1);
  INFO(errors[0].message);
  REQUIRE(errors[0].message.find("a.prefab.json -> b.prefab.json -> a.prefab.json") != std::string::npos);
  REQUIRE(!world.find(a));
  REQUIRE(!world.find(b));
  REQUIRE(world.find(plain)); // the others still load
}

TEST_CASE("make local turns an instance into ordinary entities with the same identities", "[world][prefab][nested]") {
  const Prefabs files;
  json scene;
  std::vector<core::Uuid> identities;
  {
    world::World world;
    files.load(world);
    const flecs::entity cart = world.instantiate(world.find(files.cart), "Cart 1");
    const flecs::entity axle = childNamed(world, cart, "Axle");
    childNamed(world, axle, "Hub").ensure<world::MeshRenderer>().color.r = 1.0f;
    collect(world, cart, identities);

    world.makeLocal(cart);
    std::vector<core::Uuid> after;
    collect(world, cart, after);
    REQUIRE(after == identities);
    REQUIRE(!world.isInstance(cart));
    REQUIRE(!world.isInstance(axle)); // the instance inside goes local with it
    for (const core::Uuid &uuid : after) {
      REQUIRE(!world.find(uuid).has<world::PrefabSource>());
    }
    REQUIRE(cart.owns<world::Transform>());
    REQUIRE(childNamed(world, axle, "Hub").get<world::MeshRenderer>().color.r == Approx(1.0f));
    scene = world::saveScene(world);
  }
  // Saved whole, the entities load without the prefabs.
  for (const json &entry : scene["entities"]) {
    REQUIRE(!entry.contains("prefab"));
    REQUIRE(!entry.contains("instanced"));
  }
  world::World world;
  REQUIRE(world::loadScene(world, scene).has_value());
  std::vector<core::Uuid> loaded;
  collect(world, world.roots().front(), loaded);
  REQUIRE(loaded == identities);
  REQUIRE(world.find(identities.back()).get<world::MeshRenderer>().color.r == Approx(1.0f));
}

TEST_CASE("an instance's children keep the prefab's order on every load and at every depth",
          "[world][prefab][nested]") {
  world::World world;
  // Inner: children created A, B, C, then A moved last, so the order (B, C, A) is not the ids'.
  const flecs::entity inner = world.createEntity("Inner");
  const flecs::entity a = world.createEntity("A", inner);
  world.createEntity("B", inner);
  world.createEntity("C", inner);
  world.assignOrder(a);
  world.makePrefab(inner);
  // Outer: a plain X and an instance of Inner, X moved last.
  const flecs::entity outer = world.createEntity("Outer");
  const flecs::entity x = world.createEntity("X", outer);
  world.instantiate(inner, "Nested", outer);
  world.assignOrder(x);
  world.makePrefab(outer);
  world.instantiate(outer, "Instance");

  const auto names = [&](flecs::entity parent) {
    std::vector<std::string> result;
    for (const flecs::entity child : world.children(parent)) {
      result.push_back(child.get<world::Name>().value);
    }
    return result;
  };
  const auto check = [&] {
    REQUIRE(world.roots().size() == 1);
    const flecs::entity instance = world.roots().front();
    REQUIRE(names(instance) == std::vector<std::string>{"Nested", "X"});
    REQUIRE(names(childNamed(world, instance, "Nested")) == std::vector<std::string>{"B", "C", "A"});
  };
  check();
  // The play snapshot and a tab switch are this round trip.
  for (int round = 0; round < 3; ++round) {
    const json scene = world::saveScene(world);
    world.destroyEntity(world.roots().front());
    REQUIRE(world::loadScene(world, scene).has_value());
    check();
  }
}
