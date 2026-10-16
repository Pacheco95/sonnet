// SPIKE (#304): throwaway. What does rebuilding a model's prefab do to prefab files that nest it?
#include "RefreshSpikeSupport.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <vector>

using namespace sonnet;
using namespace sonnet::spike;
using Catch::Approx;
using nlohmann::json;

namespace {

constexpr float Sentinel = -1.0f;

enum class Order {
  DependentsFirst, // Outer, Holder, model
  ModelFirst       // model, then the others: what a naive refresh would do
};

struct Nest {
  core::Uuid model = core::Uuid::generate();
  core::Uuid holder = core::Uuid::generate();
  core::Uuid inner = core::Uuid::generate();
  core::Uuid outer = core::Uuid::generate();
  core::Uuid mid = core::Uuid::generate();
  core::Uuid scene = core::Uuid::generate();
  bool twoLevel;
  world::World world;

  explicit Nest(bool two) : twoLevel(two) {
    buildModel(modelOf({{"Set", -1, 0.0f}, {"A", 0, 10.0f}, {"B", 0, 10.0f}}));
    load();
  }

  static json nestingDocument(const core::Uuid &root, const char *rootName, const core::Uuid &child,
                              const char *childName, const core::Uuid &uses) {
    return {{"version", world::SceneVersion},
            {"entities",
             {{{"uuid", root.toString()}, {"name", rootName}, {"components", json::object()}},
              {{"uuid", child.toString()},
               {"name", childName},
               {"parent", root.toString()},
               {"prefab", uses.toString()},
               {"components", json::object()}}}}};
  }

  // Outer first, as a file listing may give it.
  std::vector<world::PrefabDocument> documents() const {
    std::vector<world::PrefabDocument> docs;
    if (twoLevel) {
      docs.push_back({"outer.prefab.json", nestingDocument(outer, "Outer", mid, "Mid", holder), {}});
    }
    docs.push_back({"holder.prefab.json", nestingDocument(holder, "Holder", inner, "Inner", model), {}});
    return docs;
  }

  core::Uuid top() const { return twoLevel ? outer : holder; }
  std::string base() const { return twoLevel ? "Mid/Inner" : "Inner"; }

  void buildModel(const assets::Model &m) { static_cast<void>(buildPrefab(world, m, model)); }
  void load() {
    const std::vector<core::Error> errors = world::loadPrefabs(world, documents());
    for (const core::Error &error : errors) {
      WARN("loadPrefabs: " << error.message);
    }
    REQUIRE(errors.empty());
  }

  std::optional<float> light(const char *name) const {
    const flecs::entity instance = world.find(scene);
    if (!instance) {
      return std::nullopt;
    }
    const flecs::entity node = world.findByPath(instance, base() + "/Set/" + name);
    if (!node || !node.has<world::PointLight>()) {
      return std::nullopt;
    }
    return node.get<world::PointLight>().intensity;
  }
  flecs::entity node(const char *name) const {
    const flecs::entity instance = world.find(scene);
    return instance ? world.findByPath(instance, base() + "/Set/" + name) : flecs::entity{};
  }

  void instantiateAndOverride() {
    const flecs::entity top = world.find(this->top());
    REQUIRE(top);
    const flecs::entity instance = world.instantiate(top, "Scene", {}, scene);
    world.progress(0.016f);
    node("B").set<world::PointLight>({.color = {1, 1, 1}, .intensity = 99.0f, .range = 5.0f, .castsShadows = false});
    static_cast<void>(instance);
  }

  // Depth of a document in the nesting: 0 for one that nests only the model.
  int depthOf(const core::Uuid &root) const {
    const std::vector<world::PrefabDocument> docs = documents();
    const std::function<int(const std::string &)> depth = [&](const std::string &id) -> int {
      for (const world::PrefabDocument &doc : docs) {
        for (const json &entry : doc.document["entities"]) {
          if (!entry.contains("parent") && entry["uuid"] == id) {
            int result = 0;
            for (const json &use : doc.document["entities"]) {
              if (use.contains("prefab") && use["prefab"].get<std::string>() != model.toString()) {
                result = std::max(result, 1 + depth(use["prefab"].get<std::string>()));
              }
            }
            return result;
          }
        }
      }
      return 0;
    };
    return depth(root.toString());
  }

  // Destroys the nesting prefabs and the model prefab. Instances are gone already (clearScene).
  void destroyPrefabs(Order order) {
    std::vector<core::Uuid> nesting{holder};
    if (twoLevel) {
      nesting.push_back(outer);
    }
    std::ranges::sort(nesting, [&](const core::Uuid &a, const core::Uuid &b) { return depthOf(a) > depthOf(b); });
    if (order == Order::ModelFirst) {
      world.destroyEntity(world.find(model));
    }
    for (const core::Uuid &id : nesting) {
      world.destroyEntity(world.find(id));
    }
    if (order == Order::DependentsFirst) {
      world.destroyEntity(world.find(model));
    }
  }

  std::size_t prefabCount(const core::Uuid &id) const {
    std::size_t count = 0;
    for (const flecs::entity prefab : world.prefabs()) {
      count += world.uuidOf(prefab) == id ? 1U : 0U;
    }
    return count;
  }

  // The checks the report is built from. CHECK, so every failure shows.
  void verify(const std::string &label) {
    world.progress(0.016f);
    INFO(label);
    const auto a = light("A");
    const auto b = light("B");
    CHECK(a.value_or(Sentinel) == Approx(4.0f));
    CHECK(b.value_or(Sentinel) == Approx(99.0f));
    CHECK(node("New"));
    CHECK(node("B").is_valid());
    const json saved = world::saveScene(world);
    std::size_t instanced = 0;
    core::Uuid instancedId;
    for (const json &entry : saved["entities"]) {
      if (entry.value("instanced", false)) {
        ++instanced;
        instancedId = *core::Uuid::parse(entry["uuid"].get<std::string>());
      }
    }
    CHECK(instanced == 1);
    // The override sits on B: the saved entry's identity is B's identity.
    CHECK((node("B") && world.uuidOf(node("B")) == instancedId));
    CHECK(prefabCount(model) == 1);
    CHECK(world.prefabs().size() == (twoLevel ? 3U : 2U));
    WARN(label << ": A=" << a.value_or(Sentinel) << " B=" << b.value_or(Sentinel)
               << " New=" << (node("New") ? "yes" : "no") << " instanced=" << instanced
               << " prefabs=" << world.prefabs().size());
  }
};

const assets::Model Reexported = modelOf({{"Set", -1, 0.0f}, {"A", 0, 4.0f}, {"New", 0, 7.0f}, {"B", 0, 10.0f}});

flecs::entity childNamed(const world::World &world, flecs::entity parent, std::string_view name) {
  for (const flecs::entity child : world.children(parent)) {
    if (child.get<world::Name>().value == name) {
      return child;
    }
  }
  return {};
}

// ---- Strategy 1: save, clear, destroy every prefab, rebuild, load prefabs, load scene. ----
double reloadEverything(Nest &nest, Order order) {
  const auto start = std::chrono::steady_clock::now();
  const json scene = world::saveScene(nest.world);
  nest.world.clearScene();
  nest.destroyPrefabs(order);
  nest.buildModel(Reexported);
  nest.load();
  const auto loaded = world::loadScene(nest.world, scene);
  REQUIRE(loaded.has_value());
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

} // namespace

TEST_CASE("nested refresh: strategy 1, reload everything (one level)", "[nested-refresh]") {
  Nest nest{false};
  nest.instantiateAndOverride();
  const double ms = reloadEverything(nest, Order::DependentsFirst);
  nest.verify("strategy 1, one level");
  WARN("strategy 1, one level: " << ms << " ms");
}

TEST_CASE("nested refresh: strategy 1, reload everything (two levels)", "[nested-refresh]") {
  Nest nest{true};
  nest.instantiateAndOverride();
  const double ms = reloadEverything(nest, Order::DependentsFirst);
  nest.verify("strategy 1, two levels");
  WARN("strategy 1, two levels: " << ms << " ms");
}

TEST_CASE("nested refresh: strategy 1 with the model destroyed first", "[nested-refresh]") {
  Nest nest{true};
  nest.instantiateAndOverride();
  const json scene = world::saveScene(nest.world);
  nest.world.clearScene();
  nest.destroyPrefabs(Order::ModelFirst);
  WARN("model first: prefabs left after destroying all = " << nest.world.prefabs().size());
  nest.buildModel(Reexported);
  nest.load();
  REQUIRE(world::loadScene(nest.world, scene).has_value());
  nest.verify("strategy 1, model destroyed first");
}

TEST_CASE("nested refresh: Q1, the state of a nesting prefab after the model prefab is destroyed", "[nested-refresh]") {
  Nest nest{false};
  const flecs::entity holder = nest.world.find(nest.holder);
  const flecs::entity inner = childNamed(nest.world, holder, "Inner");
  REQUIRE(inner);
  const core::Uuid innerId = nest.world.uuidOf(inner);
  const flecs::entity a = childNamed(nest.world, childNamed(nest.world, inner, "Set"), "A");
  REQUIRE(a);
  const core::Uuid aId = nest.world.uuidOf(a);
  WARN("before: Inner IsA model = " << (inner.target(flecs::IsA) == nest.world.find(nest.model))
                                    << ", copy children of Inner = " << nest.world.children(inner).size());

  nest.world.destroyEntity(nest.world.find(nest.model));

  const flecs::entity holderAfter = nest.world.find(nest.holder);
  const flecs::entity innerAfter = nest.world.find(innerId);
  const flecs::entity aAfter = nest.world.find(aId);
  WARN("after destroy: Holder alive=" << holderAfter.is_valid() << ", Inner alive=" << innerAfter.is_valid()
                                      << ", Inner/Set/A alive=" << aAfter.is_valid());
  if (innerAfter) {
    WARN("  Inner IsA target alive=" << innerAfter.target(flecs::IsA).is_valid()
                                     << ", children=" << nest.world.children(innerAfter).size()
                                     << ", has Prefab=" << innerAfter.has(flecs::Prefab));
  }
  if (aAfter) {
    WARN("  Inner/Set/A has PointLight=" << aAfter.has<world::PointLight>() << " intensity="
                                         << (aAfter.has<world::PointLight>() ? aAfter.get<world::PointLight>().intensity : Sentinel)
                                         << ", has PrefabSource=" << aAfter.has<world::PrefabSource>());
  }
  nest.buildModel(Reexported);
  WARN("after rebuild: Inner IsA target alive=" << (innerAfter && innerAfter.target(flecs::IsA).is_valid())
                                                << ", Inner IsA the new model prefab="
                                                << (innerAfter && innerAfter.target(flecs::IsA) == nest.world.find(nest.model)));
  // The nesting prefab, instantiated now: does it show the new values?
  const flecs::entity top = nest.world.find(nest.holder);
  if (top) {
    const flecs::entity probe = nest.world.instantiate(top, "Probe");
    nest.world.progress(0.016f);
    const flecs::entity aa = nest.world.findByPath(probe, "Inner/Set/A");
    const flecs::entity nn = nest.world.findByPath(probe, "Inner/Set/New");
    WARN("  instance of stale Holder: A="
         << (aa && aa.has<world::PointLight>() ? aa.get<world::PointLight>().intensity : Sentinel)
         << " New=" << (nn ? "yes" : "no"));
  }
  // Loading the nesting prefab again, with the stale one still in the world.
  const std::vector<core::Error> errors = world::loadPrefabs(nest.world, nest.documents());
  for (const core::Error &error : errors) {
    WARN("reload over the stale prefab: " << error.message);
  }
  CHECK(!errors.empty());
}

TEST_CASE("nested refresh: strategy 2, model prefab only (refreshByReload)", "[nested-refresh]") {
  Nest nest{false};
  nest.instantiateAndOverride();
  const json scene = world::saveScene(nest.world);
  nest.world.clearScene();
  nest.world.destroyEntity(nest.world.find(nest.model));
  nest.buildModel(Reexported);
  const auto loaded = world::loadScene(nest.world, scene);
  WARN("strategy 2 loadScene ok=" << loaded.has_value() << (loaded ? "" : " " + loaded.error().message));
  nest.verify("strategy 2");
}

TEST_CASE("nested refresh: strategy 3, in-place merge", "[nested-refresh]") {
  Nest nest{false};
  nest.instantiateAndOverride();
  refreshByMerge(nest.world, nest.world.find(nest.model), Reexported);
  nest.verify("strategy 3 (scene instance of the nesting prefab)");
  // The copies inside the nesting prefab itself.
  const flecs::entity inner = childNamed(nest.world, nest.world.find(nest.holder), "Inner");
  const flecs::entity set = childNamed(nest.world, inner, "Set");
  const flecs::entity a = childNamed(nest.world, set, "A");
  WARN("strategy 3, inside Holder: Inner/Set/A intensity="
       << (a && a.has<world::PointLight>() ? a.get<world::PointLight>().intensity : Sentinel)
       << " New=" << (childNamed(nest.world, set, "New") ? "yes" : "no"));
}
