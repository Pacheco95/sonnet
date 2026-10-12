// Spike for ADR-0028 (nested scene instances): what flecs 4 does with a prefab whose child is an
// instance of another prefab, and whether derived identities make an instance's descendants
// addressable in a scene. Throwaway; the findings go into the ADR.
#include <sonnet/world/Scene.h>
#include <sonnet/world/World.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>
#include <vector>

using namespace sonnet;
using Catch::Approx;

namespace {

// Wheel: a root with a "Hub" child. Cart: a root with a plain "Body" and an "Axle" that is an
// instance of Wheel. Both as their files would be written.
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

  nlohmann::json wheelFile() const {
    nlohmann::json entities = {{{"uuid", wheel.toString()}, {"name", "Wheel"}, {"components", nlohmann::json::object()}}};
    if (withHub) {
      entities.push_back(
          {{"uuid", hub.toString()},
           {"name", "Hub"},
           {"parent", wheel.toString()},
           {"components",
            {{"MeshRenderer", {{"color", {{"x", hubGrey}, {"y", hubGrey}, {"z", hubGrey}, {"w", 1.0}}}}},
             {"Transform", {{"position", {{"x", 0.0}, {"y", hubHeight}, {"z", 0.0}}}}}}}});
    }
    return {{"version", world::SceneVersion}, {"entities", entities}};
  }

  nlohmann::json cartFile() const {
    nlohmann::json bodyComponents = {{"Static", nullptr}};
    if (bodySpins) {
      bodyComponents["Spin"] = {{"speed", 3.0}};
    }
    return {{"version", world::SceneVersion},
            {"entities",
             {{{"uuid", cart.toString()}, {"name", "Cart"}, {"components", nlohmann::json::object()}},
              {{"uuid", body.toString()}, {"name", "Body"}, {"parent", cart.toString()}, {"components", bodyComponents}},
              {{"uuid", axle.toString()},
               {"name", "Axle"},
               {"parent", cart.toString()},
               {"prefab", wheel.toString()},
               {"components", nlohmann::json::object()}}}}};
  }

  void load(world::World &world) const {
    REQUIRE(world::loadPrefab(world, wheelFile()).has_value());
    REQUIRE(world::loadPrefab(world, cartFile()).has_value());
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

TEST_CASE("spike Q1/Q2: flecs copies a prefab's children, nested ones included, with no link back",
          "[world][spike]") {
  world::World world;
  const Prefabs files;
  files.load(world);
  const flecs::entity cart = world.instantiate(world.find(files.cart), "Cart 1");

  const flecs::entity axle = childNamed(world, cart, "Axle");
  REQUIRE(axle);
  const flecs::entity hub = childNamed(world, axle, "Hub");
  REQUIRE(hub); // the grandchild from the inner prefab is created
  // The nested instance root is IsA the inner prefab, as written in the outer prefab's file...
  REQUIRE(axle.target(flecs::IsA) == world.find(files.wheel));
  // ...but a copied child is not IsA anything and owns every component it has: an instance child
  // shares nothing with the prefab child after instantiation, so `owns` cannot find overrides.
  REQUIRE(!hub.target(flecs::IsA));
  REQUIRE(hub.owns<world::MeshRenderer>());
  // Changing the prefab child afterwards does not reach the copy.
  world.find(files.hub).ensure<world::MeshRenderer>().color.r = 0.25f;
  REQUIRE(hub.get<world::MeshRenderer>().color.r == Approx(0.5f));
}

TEST_CASE("spike Q3: derived identities are the same on every load and distinct per instance", "[world][spike]") {
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

TEST_CASE("spike Q4: a scene keeps edits inside an instance and takes later prefab changes", "[world][spike]") {
  Prefabs files;
  nlohmann::json scene;
  core::Uuid cartUuid;
  core::Uuid hubUuid;
  {
    world::World world;
    files.load(world);
    const flecs::entity cart = world.instantiate(world.find(files.cart), "Cart 1");
    cartUuid = world.uuidOf(cart);
    const flecs::entity body = childNamed(world, cart, "Body");
    const flecs::entity axle = childNamed(world, cart, "Axle");
    const flecs::entity hub = childNamed(world, axle, "Hub");
    hubUuid = world.uuidOf(hub);
    hub.ensure<world::MeshRenderer>().color.r = 1.0f; // an override two levels down
    body.add<world::Disabled>();                      // a source child hidden
    axle.set<world::Name>({"Front axle"});            // a rename
    world.createEntity("Lamp", body);                 // a child added by hand under an instance child

    scene = world::saveScene(world);
    INFO(scene.dump(2));
    // The instance, three instanced entries and the added child; the unchanged nothing.
    REQUIRE(scene["entities"].size() == 5);
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
    // The overridden component is the scene's, whole: red from the scene, green and blue from the
    // MeshRenderer as it was saved, not the prefab's new grey.
    REQUIRE(hub.get<world::MeshRenderer>().color.r == Approx(1.0f));
    REQUIRE(hub.get<world::MeshRenderer>().color.g == Approx(0.5f));
    // A component the instance did not change follows the prefab.
    REQUIRE(hub.get<world::Transform>().position.y == Approx(2.0f));
    REQUIRE(body.has<world::Disabled>());
    REQUIRE(body.has<world::Spin>()); // added to the prefab later, reaches the instance
    REQUIRE(childNamed(world, body, "Lamp"));
  }
}

TEST_CASE("spike Q4b: an override inside a prefab of prefabs reaches its instances", "[world][spike]") {
  const Prefabs files;
  nlohmann::json cartFile;
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
    REQUIRE(cartFile["entities"].size() == 3); // Cart, Axle, the Hub's override
  }
  world::World world;
  REQUIRE(world::loadPrefab(world, files.wheelFile()).has_value());
  REQUIRE(world::loadPrefab(world, cartFile).has_value());
  const flecs::entity cart = world.instantiate(world.find(cartUuid), "Cart 1");
  const flecs::entity hub = childNamed(world, childNamed(world, cart, "Axle"), "Hub");
  REQUIRE(hub.get<world::MeshRenderer>().color.r == Approx(1.0f));
}

TEST_CASE("spike Q5: a deleted instance comes back with its descendants' identities and edits", "[world][spike]") {
  const Prefabs files;
  world::World world;
  files.load(world);
  const flecs::entity cart = world.instantiate(world.find(files.cart), "Cart 1");
  const flecs::entity hub = childNamed(world, childNamed(world, cart, "Axle"), "Hub");
  hub.ensure<world::MeshRenderer>().color.r = 1.0f;
  world.createEntity("Lamp", childNamed(world, cart, "Body"));
  const core::Uuid hubUuid = world.uuidOf(hub);

  // What the editor's delete command keeps, and what its undo loads back.
  const nlohmann::json kept = world::saveSubtree(world, cart);
  world.destroyEntity(cart);
  REQUIRE(!world.find(hubUuid));
  REQUIRE(world::loadScene(world, kept).has_value());
  const flecs::entity restored = world.find(hubUuid);
  REQUIRE(restored);
  REQUIRE(restored.get<world::MeshRenderer>().color.r == Approx(1.0f));
  REQUIRE(childNamed(world, childNamed(world, world.roots().front(), "Body"), "Lamp"));
}

TEST_CASE("spike: an override whose target left the prefab is skipped with a warning", "[world][spike]") {
  Prefabs files;
  nlohmann::json scene;
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
}
