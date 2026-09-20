#include <sonnet/world/World.h>

#include <sonnet/core/Assert.h>
#include <sonnet/core/Log.h>
#include <sonnet/core/Profile.h>

#include <algorithm>
#include <cstddef>

namespace sonnet::world {

namespace {

// Systems that only run in play mode carry this tag; the edit pipeline excludes them.
struct Simulation {};

constexpr std::array<const char *, 5> PhaseNames{"Input", "FixedUpdate", "Update", "PostUpdate", "PreRender"};

// GLM's quaternion layout depends on its configuration, so the offsets are measured, not assumed.
template <typename Component, typename Member>
std::size_t offsetOf(const Component &component, const Member &member) noexcept {
  return static_cast<std::size_t>(reinterpret_cast<const std::byte *>(&member) -
                                  reinterpret_cast<const std::byte *>(&component));
}

// Instantiated children are new entities: they need identities and world transforms of their
// own. The children are collected first, since adding to them while iterating is not allowed.
void adoptInstantiatedChildren(World &world, flecs::entity node) {
  for (const flecs::entity child : world.children(node)) {
    if (!child.has<Identity>()) {
      child.set<Identity>({core::Uuid::generate()});
    }
    child.add<WorldTransform>();
    adoptInstantiatedChildren(world, child);
  }
}

} // namespace

World::World(const WorldDesc &desc) : m_fixedDelta(desc.fixedDelta), m_maxFixedSteps(desc.maxFixedSteps) {
  SONNET_ASSERT(m_fixedDelta > 0.0f && m_maxFixedSteps > 0, "the fixed timestep needs a positive step and count");
  m_world.import <flecs::units>();
  if (desc.explorer) {
    m_world.import <flecs::stats>();
    m_world.set<flecs::Rest>({});
    SONNET_LOG_INFO("flecs explorer enabled: https://www.flecs.dev/explorer");
  }
  registerComponents();

  flecs::entity previous;
  for (std::size_t i = 0; i < m_phases.size(); ++i) {
    m_phases[i] = m_world.entity(PhaseNames[i]).add(flecs::Phase);
    if (previous) {
      m_phases[i].depends_on(previous);
    }
    previous = m_phases[i];
  }
  m_world.component<Simulation>("Simulation");
  const flecs::entity input = phase(Phase::Input);
  const flecs::entity fixed = phase(Phase::FixedUpdate);
  // Every pipeline runs enabled systems of enabled phases; they differ in which phases they
  // take and whether simulation systems are among them. Builders are built in place: they
  // point into their own descriptors and are not safe to copy.
  enum class Stage : std::uint8_t {
    Input,
    Fixed,
    Frame
  };
  const auto pipeline = [&](Stage stage, bool simulation) {
    auto builder = m_world.pipeline();
    builder.with(flecs::System)
        .with(flecs::Phase)
        .cascade(flecs::DependsOn)
        .without(flecs::Disabled)
        .up(flecs::DependsOn)
        .without(flecs::Disabled)
        .up(flecs::ChildOf);
    if (stage == Stage::Input) {
      builder.with(flecs::DependsOn, input);
    } else if (stage == Stage::Fixed) {
      builder.with(flecs::DependsOn, fixed);
    } else {
      builder.without(flecs::DependsOn, input).without(flecs::DependsOn, fixed);
    }
    if (!simulation) {
      builder.without<Simulation>();
    }
    return builder.build();
  };
  for (const bool playing : {false, true}) {
    m_inputPipelines[playing ? 1 : 0] = pipeline(Stage::Input, playing);
    m_framePipelines[playing ? 1 : 0] = pipeline(Stage::Frame, playing);
  }
  m_fixedPipeline = pipeline(Stage::Fixed, true);
  registerSystems();

  m_world.observer<const Identity>("IdentityIndex")
      .event(flecs::OnSet)
      .each([this](flecs::entity entity, const Identity &identity) { m_byUuid[identity.uuid] = entity.id(); });
  m_world.observer<const Identity>("IdentityUnindex")
      .event(flecs::OnRemove)
      .each([this](flecs::entity, const Identity &identity) { m_byUuid.erase(identity.uuid); });
  SONNET_LOG_DEBUG("world ready with {} components", m_components.size());
}

World::~World() {
  // The remove observers run while the flecs world is torn down and touch the index, which is
  // declared before the world so it is still alive; the world is destroyed first.
}

void World::registerComponents() {
  using Radians = flecs::units::angle::Radians;
  {
    const glm::vec3 v{};
    m_world.component<glm::vec3>("vec3")
        .member<float>("x", 0, offsetOf(v, v.x))
        .member<float>("y", 0, offsetOf(v, v.y))
        .member<float>("z", 0, offsetOf(v, v.z));
    const glm::vec4 v4{};
    m_world.component<glm::vec4>("vec4")
        .member<float>("x", 0, offsetOf(v4, v4.x))
        .member<float>("y", 0, offsetOf(v4, v4.y))
        .member<float>("z", 0, offsetOf(v4, v4.z))
        .member<float>("w", 0, offsetOf(v4, v4.w));
    const glm::quat q{};
    m_world.component<glm::quat>("quat")
        .member<float>("x", 0, offsetOf(q, q.x))
        .member<float>("y", 0, offsetOf(q, q.y))
        .member<float>("z", 0, offsetOf(q, q.z))
        .member<float>("w", 0, offsetOf(q, q.w));
  }
  // Structural components: the identity is per entity, the world transform is derived, the name
  // is the scene file's envelope. None is inherited from a prefab except the name, which an
  // instance's children show until renamed.
  m_world.component<Identity>("Identity").add(flecs::OnInstantiate, flecs::DontInherit);
  m_world.component<WorldTransform>("WorldTransform").add(flecs::OnInstantiate, flecs::DontInherit);
  m_world.component<Name>("Name").add(flecs::OnInstantiate, flecs::Inherit);

  // Asset references serialize as their canonical string; an unparsable string is nil.
  m_world.component<core::Uuid>("Uuid")
      .opaque(flecs::String)
      .serialize([](const flecs::serializer *serializer, const core::Uuid *uuid) {
        const std::string text = uuid->toString();
        const char *chars = text.c_str();
        return serializer->value(flecs::String, &chars);
      })
      .assign_string([](core::Uuid *uuid, const char *value) {
        *uuid = core::Uuid::parse(value != nullptr ? value : "").value_or(core::Uuid{});
      });
  registerComponent<Transform>("Transform");
  m_world.component<Transform>().member<glm::vec3>("position").member<glm::quat>("rotation").member<glm::vec3>("scale");
  registerComponent<MeshRenderer>("MeshRenderer");
  m_world.component<MeshRenderer>()
      .member<core::Uuid>("mesh")
      .member<core::Uuid>("material")
      .member<glm::vec4>("color")
      .member<bool>("visible");
  registerComponent<Camera>("Camera");
  m_world.component<Camera>().member<float, Radians>("fovY").member<float>("nearPlane");
  registerComponent<DirectionalLight>("DirectionalLight");
  m_world.component<DirectionalLight>().member<glm::vec3>("color").member<float>("intensity");
  registerComponent<PointLight>("PointLight");
  m_world.component<PointLight>().member<glm::vec3>("color").member<float>("intensity").member<float>("range");
  registerComponent<SpotLight>("SpotLight");
  m_world.component<SpotLight>()
      .member<glm::vec3>("color")
      .member<float>("intensity")
      .member<float>("range")
      .member<float, Radians>("innerAngle")
      .member<float, Radians>("outerAngle");
  registerComponent<Environment>("Environment");
  m_world.component<Environment>().member<core::Uuid>("map").member<float>("intensity").member<float>("exposure");
  registerComponent<SkinnedMesh>("SkinnedMesh").member<core::Uuid>("skin");
  registerComponent<Animator>("Animator")
      .member<core::Uuid>("clip")
      .member<float>("time")
      .member<float>("speed")
      .member<bool>("playing")
      .member<bool>("loop");
  // Derived every frame for the entity itself, like WorldTransform: never inherited, never saved.
  m_world.component<SkinPose>("SkinPose").add(flecs::OnInstantiate, flecs::DontInherit);
  registerComponent<Spin>("Spin");
  m_world.component<Spin>().member<glm::vec3>("axis").member<float>("speed");
  registerComponent<Static>("Static", true);
  registerComponent<EditorOnly>("EditorOnly", true);
  registerComponent<Disabled>("Disabled", true);
}

void World::registerSystems() {
  // Parents first (cascade), so a child multiplies an up-to-date parent matrix.
  m_transformSystem = m_world.system<const Transform, WorldTransform, const WorldTransform *>("TransformSystem")
                          .kind(phase(Phase::PreRender))
                          .term_at(2)
                          .parent()
                          .cascade()
                          .each([](const Transform &local, WorldTransform &world, const WorldTransform *parent) {
                            world.matrix = parent != nullptr ? parent->matrix * local.matrix() : local.matrix();
                          });

  m_world.system<Transform, const Spin>("SpinSystem")
      .kind(phase(Phase::Update))
      .without<Disabled>()
      .each([](flecs::iter &it, std::size_t, Transform &transform, const Spin &spin) {
        const float angle = spin.speed * it.delta_time();
        const glm::vec3 axis = glm::length(spin.axis) > 0.0f ? glm::normalize(spin.axis) : glm::vec3{0.0f, 1.0f, 0.0f};
        transform.rotation = glm::normalize(glm::angleAxis(angle, axis) * transform.rotation);
      })
      .add<Simulation>();
}

void World::addToSimulation(flecs::entity system) {
  system.add<Simulation>();
}

flecs::entity World::createEntity(std::string_view name, flecs::entity parent, core::Uuid uuid) {
  flecs::entity entity = m_world.entity();
  entity.set<Identity>({uuid.isNil() ? core::Uuid::generate() : uuid});
  entity.set<Name>({std::string{name}});
  entity.add<Transform>();
  entity.add<WorldTransform>();
  if (parent) {
    entity.child_of(parent);
  }
  return entity;
}

void World::destroyEntity(flecs::entity entity) {
  if (entity && entity.is_alive()) {
    entity.destruct();
  }
}

void World::setParent(flecs::entity entity, flecs::entity parent) {
  SONNET_ASSERT(!parent || !isDescendant(parent, entity), "reparenting an entity under its own descendant");
  const glm::mat4 world = worldMatrix(entity);
  entity.remove(flecs::ChildOf, flecs::Wildcard);
  glm::mat4 local = world;
  if (parent) {
    entity.child_of(parent);
    local = glm::inverse(worldMatrix(parent)) * world;
  }
  entity.set<Transform>(Transform::fromMatrix(local));
}

flecs::entity World::parentOf(flecs::entity entity) const {
  return entity.parent();
}

std::vector<flecs::entity> World::roots() const {
  std::vector<flecs::entity> result;
  m_world.query_builder<const Identity>()
      .without(flecs::ChildOf, flecs::Wildcard)
      .build()
      .each([&](flecs::entity entity, const Identity &) { result.push_back(entity); });
  // Table order is not creation order; ids nearly are, and a stable order is what a panel needs.
  std::ranges::sort(result, [](flecs::entity a, flecs::entity b) { return pickId(a) < pickId(b); });
  return result;
}

std::vector<flecs::entity> World::children(flecs::entity entity) const {
  std::vector<flecs::entity> result;
  entity.children([&](flecs::entity child) { result.push_back(child); });
  std::ranges::sort(result, [](flecs::entity a, flecs::entity b) { return pickId(a) < pickId(b); });
  return result;
}

glm::mat4 World::worldMatrix(flecs::entity entity) {
  glm::mat4 matrix{1.0f};
  for (flecs::entity current = entity; current; current = current.parent()) {
    if (const Transform *transform = current.try_get<Transform>()) {
      matrix = transform->matrix() * matrix;
    }
  }
  return matrix;
}

flecs::entity World::findByPath(flecs::entity root, std::string_view path) const {
  flecs::entity current = root;
  while (current && !path.empty()) {
    const std::size_t slash = path.find('/');
    const std::string_view name = path.substr(0, slash);
    path = slash == std::string_view::npos ? std::string_view{} : path.substr(slash + 1);
    flecs::entity match;
    for (const flecs::entity child : children(current)) {
      const Name *childName = child.try_get<Name>();
      if (childName != nullptr && childName->value == name) {
        match = child;
        break;
      }
    }
    current = match;
  }
  return current;
}

bool World::isDescendant(flecs::entity entity, flecs::entity ancestor) const {
  for (flecs::entity current = entity.parent(); current; current = current.parent()) {
    if (current == ancestor) {
      return true;
    }
  }
  return false;
}

void World::clearScene() {
  for (const flecs::entity root : roots()) {
    if (!root.has<EditorOnly>()) {
      root.destruct();
    }
  }
}

void World::clearPrefabs() {
  for (const flecs::entity prefab : prefabs()) {
    prefab.destruct();
  }
}

std::vector<flecs::entity> World::prefabs() const {
  std::vector<flecs::entity> result;
  m_world.query_builder<const Identity>()
      .with(flecs::Prefab)
      .without(flecs::ChildOf, flecs::Wildcard)
      .build()
      .each([&](flecs::entity entity, const Identity &) { result.push_back(entity); });
  std::ranges::sort(result, [](flecs::entity a, flecs::entity b) { return pickId(a) < pickId(b); });
  return result;
}

flecs::entity World::find(const core::Uuid &uuid) const {
  const auto it = m_byUuid.find(uuid);
  if (it == m_byUuid.end()) {
    return {};
  }
  const flecs::entity entity{m_world, it->second};
  return entity.is_alive() ? entity : flecs::entity{};
}

core::Uuid World::uuidOf(flecs::entity entity) const {
  const Identity *identity = entity ? entity.try_get<Identity>() : nullptr;
  return identity != nullptr ? identity->uuid : core::Uuid{};
}

std::uint32_t World::pickId(flecs::entity entity) noexcept {
  return static_cast<std::uint32_t>(entity.id() & 0xFFFFFFFFu);
}

flecs::entity World::fromPickId(std::uint32_t id) const {
  if (id == 0) {
    return {};
  }
  return m_world.get_alive(id);
}

flecs::entity World::instantiate(flecs::entity prefab, std::string_view name, flecs::entity parent) {
  SONNET_ASSERT(prefab.has(flecs::Prefab), "instantiating an entity that is not a prefab");
  flecs::entity entity = m_world.entity().is_a(prefab);
  entity.set<Identity>({core::Uuid::generate()});
  entity.set<Name>({std::string{name}});
  // Own copies of what places the instance; the rest stays shared until edited.
  const Transform *transform = prefab.try_get<Transform>();
  entity.set<Transform>(transform != nullptr ? *transform : Transform{});
  entity.add<WorldTransform>();
  if (parent) {
    entity.child_of(parent);
  }
  adoptInstantiatedChildren(*this, entity);
  return entity;
}

flecs::entity World::prefabOf(flecs::entity entity) const {
  if (!entity) {
    return {};
  }
  const flecs::entity base = entity.target(flecs::IsA);
  return base && base.has(flecs::Prefab) && !base.parent() ? base : flecs::entity{};
}

bool World::isInstance(flecs::entity entity) const {
  return prefabOf(entity).is_valid();
}

const ComponentInfo *World::findComponent(std::string_view name) const {
  const auto it = std::ranges::find(m_components, name, &ComponentInfo::name);
  return it != m_components.end() ? &*it : nullptr;
}

const ComponentInfo *World::findComponent(flecs::entity_t id) const {
  const auto it = std::ranges::find(m_components, id, &ComponentInfo::id);
  return it != m_components.end() ? &*it : nullptr;
}

nlohmann::json World::componentToJson(flecs::entity entity, flecs::entity_t component) const {
  const ComponentInfo *info = findComponent(component);
  if (info == nullptr || info->tag || !entity.has(component)) {
    return nullptr;
  }
  return valueToJson(component, entity.get(component));
}

nlohmann::json World::valueToJson(flecs::entity_t component, const void *value) const {
  char *text = ecs_ptr_to_json(m_world, component, value);
  if (text == nullptr) {
    SONNET_LOG_ERROR("component {} could not be serialized", component);
    return nullptr;
  }
  nlohmann::json json = nlohmann::json::parse(text, nullptr, false);
  ecs_os_free(text);
  return json;
}

void World::componentFromJson(flecs::entity entity, flecs::entity_t component, const nlohmann::json &value) {
  const ComponentInfo *info = findComponent(component);
  SONNET_ASSERT(info != nullptr, "component {} is not registered", component);
  if (info->tag) {
    entity.add(component);
    return;
  }
  void *target = entity.ensure(component);
  if (!value.is_null()) {
    const std::string text = value.dump();
    ecs_from_json_desc_t desc{};
    desc.name = info->name.c_str();
    if (ecs_ptr_from_json(m_world, component, target, text.c_str(), &desc) == nullptr) {
      SONNET_LOG_ERROR("component \"{}\": {} is not a valid value", info->name, text);
    }
  }
  entity.modified(component);
}

void World::setPlaying(bool playing) {
  if (playing == m_playing) {
    return;
  }
  m_playing = playing;
  // A new run starts from a whole step, not from what was left when the last one stopped.
  m_accumulator = 0.0f;
}

void World::progress(float dt) {
  SONNET_ZONE();
  const std::size_t mode = m_playing ? 1 : 0;
  const float frameDelta = m_world.frame_begin(dt);
  m_world.run_pipeline(m_inputPipelines[mode], frameDelta);
  if (m_playing) {
    m_accumulator += frameDelta;
    std::uint32_t steps = 0;
    while (m_accumulator >= m_fixedDelta && steps < m_maxFixedSteps) {
      m_world.run_pipeline(m_fixedPipeline, m_fixedDelta);
      m_accumulator -= m_fixedDelta;
      ++steps;
    }
    // A frame too long to catch up with drops the backlog: the simulation slows down instead
    // of running ever more steps per frame.
    m_accumulator = std::min(m_accumulator, m_fixedDelta * 0.999f);
  }
  m_world.run_pipeline(m_framePipelines[mode], frameDelta);
  m_world.frame_end();
}

} // namespace sonnet::world
