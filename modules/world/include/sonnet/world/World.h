#pragma once

#include <sonnet/world/Components.h>

#include <sonnet/core/Uuid.h>

#include <flecs.h>
#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sonnet::world {

// Pipeline phases in execution order (docs/architecture.md, "Entity model"). Systems tagged as
// simulation run only in play mode; FixedUpdate runs at the fixed timestep, zero or more times
// per frame, and only in play mode.
enum class Phase : std::uint8_t {
  Input,
  FixedUpdate,
  Update,
  PostUpdate,
  PreRender,
};

// A component the inspector and the serializer know about: everything registered with
// reflection in World, by the name used in scene files.
struct ComponentInfo {
  std::string name;
  flecs::entity_t id{0};
  bool tag{false}; // no data: serialized as null
};

struct WorldDesc {
  // Serves the flecs explorer on its default port; Debug builds of the editor turn it on.
  bool explorer{false};
  // The FixedUpdate step, and how many steps one frame may run before the simulation falls
  // behind instead of spiralling (ADR-0009).
  float fixedDelta{1.0f / 60.0f};
  std::uint32_t maxFixedSteps{4};
};

// The flecs world with the engine's components, phases and systems registered. flecs types
// appear unwrapped by decision (ADR-0003): callers use flecs::entity directly and World adds
// what the engine needs on top: identities, the hierarchy helpers, prefab instantiation, the
// play-mode switch and the fixed timestep. Subsystems such as physics and scripting register
// their components and systems here (ADR-0009).
class World {
public:
  explicit World(const WorldDesc &desc = {});
  ~World();
  World(const World &) = delete;
  World &operator=(const World &) = delete;

  [[nodiscard]] flecs::world &ecs() noexcept {
    return m_world;
  }
  [[nodiscard]] const flecs::world &ecs() const noexcept {
    return m_world;
  }
  [[nodiscard]] flecs::entity phase(Phase phase) const noexcept {
    return m_phases[static_cast<std::size_t>(phase)];
  }

  // Scene entities: an Identity (fresh or given), a Name, a Transform and a WorldTransform.
  flecs::entity createEntity(std::string_view name, flecs::entity parent = {}, core::Uuid uuid = {});
  // Also deletes the children.
  void destroyEntity(flecs::entity entity);
  // Reparents (a null parent moves to the root) keeping the world transform, so the entity
  // stays where it is on screen.
  void setParent(flecs::entity entity, flecs::entity parent);
  [[nodiscard]] flecs::entity parentOf(flecs::entity entity) const;
  [[nodiscard]] std::vector<flecs::entity> roots() const;
  [[nodiscard]] std::vector<flecs::entity> children(flecs::entity entity) const;
  [[nodiscard]] bool isDescendant(flecs::entity entity, flecs::entity ancestor) const;
  // The descendant of `root` at a path of names joined by '/' ("Hips/Spine"), taking the first
  // child of each name in id order; null when a name is missing, `root` for an empty path.
  [[nodiscard]] flecs::entity findByPath(flecs::entity root, std::string_view path) const;
  // The world matrix from the hierarchy's local transforms, current even between runs of the
  // transform system.
  [[nodiscard]] static glm::mat4 worldMatrix(flecs::entity entity);
  // Deletes every scene entity; prefabs and editor-only entities stay.
  void clearScene();
  // Deletes every prefab, for switching projects. Instances lose their base first: clear the
  // scene before the prefabs.
  void clearPrefabs();
  [[nodiscard]] std::vector<flecs::entity> prefabs() const;

  [[nodiscard]] flecs::entity find(const core::Uuid &uuid) const;
  [[nodiscard]] core::Uuid uuidOf(flecs::entity entity) const;
  // The low 32 bits of a flecs id, what the id pass writes; resolves to the live entity or null.
  [[nodiscard]] static std::uint32_t pickId(flecs::entity entity) noexcept;
  [[nodiscard]] flecs::entity fromPickId(std::uint32_t id) const;

  // A prefab instance: IsA the prefab, with its own Identity, Name and Transform, and fresh
  // identities on the instantiated children. `prefab` must carry flecs::Prefab.
  flecs::entity instantiate(flecs::entity prefab, std::string_view name, flecs::entity parent = {});
  [[nodiscard]] flecs::entity prefabOf(flecs::entity entity) const;
  [[nodiscard]] bool isInstance(flecs::entity entity) const;

  // Registers a component with reflection under the name scene files use, inherited by prefab
  // instances; the caller adds the members on the returned component. `tag` for a component
  // without data.
  template <typename T> flecs::component<T> registerComponent(const char *name, bool tag = false) {
    flecs::component<T> component = m_world.component<T>(name);
    component.add(flecs::OnInstantiate, flecs::Inherit);
    m_components.push_back({name, component.id(), tag});
    return component;
  }
  // Marks a system as simulation: it runs in play mode only.
  void addToSimulation(flecs::entity system);

  [[nodiscard]] std::span<const ComponentInfo> components() const noexcept {
    return m_components;
  }
  [[nodiscard]] const ComponentInfo *findComponent(std::string_view name) const;
  [[nodiscard]] const ComponentInfo *findComponent(flecs::entity_t id) const;
  // The value of a reflected component as flecs' JSON; null for a tag or an absent component.
  [[nodiscard]] nlohmann::json componentToJson(flecs::entity entity, flecs::entity_t component) const;
  // The same for a value held outside the world, such as an undo command's copy.
  [[nodiscard]] nlohmann::json valueToJson(flecs::entity_t component, const void *value) const;
  // Adds the component when absent and assigns the fields present in `value`.
  void componentFromJson(flecs::entity entity, flecs::entity_t component, const nlohmann::json &value);

  // Play mode enables the simulation phases (docs/architecture.md, "Editor and player").
  void setPlaying(bool playing);
  [[nodiscard]] bool isPlaying() const noexcept {
    return m_playing;
  }
  // Runs the frame's phases, with as many fixed steps as the accumulated time holds in play
  // mode; world transforms are up to date afterwards.
  void progress(float dt);
  [[nodiscard]] float fixedDelta() const noexcept {
    return m_fixedDelta;
  }
  // How far the accumulator is into the next fixed step, in [0, 1): what rendering interpolates
  // simulated poses by.
  [[nodiscard]] float fixedAlpha() const noexcept {
    return m_accumulator / m_fixedDelta;
  }

private:
  void registerComponents();
  void registerSystems();

  // Declared before m_world: its OnRemove observer runs while the world is torn down
  // (World::~World()), so the index must still be alive when the world is destroyed first.
  std::unordered_map<core::Uuid, flecs::entity_t> m_byUuid;
  flecs::world m_world;
  std::array<flecs::entity, 5> m_phases;
  // Input, then the fixed steps, then the rest of the frame: one pipeline each, the first and
  // last in an edit and a play variant, indexed by m_playing.
  std::array<flecs::entity, 2> m_inputPipelines;
  std::array<flecs::entity, 2> m_framePipelines;
  flecs::entity m_fixedPipeline;
  flecs::entity m_transformSystem;
  std::vector<ComponentInfo> m_components;
  float m_fixedDelta;
  std::uint32_t m_maxFixedSteps;
  float m_accumulator{0.0f};
  bool m_playing{false};
};

} // namespace sonnet::world
