#include <sonnet/world/Animation.h>

#include <sonnet/world/Components.h>

#include <sonnet/assets/AssetDatabase.h>
#include <sonnet/core/Log.h>
#include <sonnet/core/Profile.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <string>

namespace sonnet::world {

namespace {

std::string describe(flecs::entity entity) {
  const Name *name = entity.try_get<Name>();
  return name != nullptr ? name->value : std::format("entity {}", entity.id());
}

// Advances the time by the frame, wrapping a looping clip and stopping one that does not at the
// end it runs into, which is the start when playing backwards.
void advance(Animator &animator, float duration, float dt) {
  if (!animator.playing) {
    return;
  }
  animator.time += dt * animator.speed;
  if (duration <= 0.0f) {
    animator.time = 0.0f;
  } else if (animator.loop) {
    animator.time = std::fmod(animator.time, duration);
    if (animator.time < 0.0f) {
      animator.time += duration;
    }
  } else if (animator.time >= duration || animator.time <= 0.0f) {
    animator.time = std::clamp(animator.time, 0.0f, duration);
    animator.playing = animator.speed == 0.0f;
  }
}

} // namespace

AnimationSystem::AnimationSystem(World &world, assets::AssetDatabase &assets) : m_world(world), m_assets(assets) {
  flecs::world &ecs = world.ecs();
  m_animators = ecs.query_builder<>("Animators").with<Animator>().without<Disabled>().build();
  m_skinned = ecs.query_builder<>("SkinnedMeshes").with<SkinnedMesh>().without<Disabled>().build();
  // Immediate with deferring suspended, as the scripts' systems: the poses are written to
  // instances whose Transforms are inherited from their prefab until then.
  m_playback =
      ecs.system("AnimationPlayback").kind(world.phase(Phase::Update)).immediate().run([this](flecs::iter &it) {
        play(it.delta_time());
      });
  world.addToSimulation(m_playback);
  // Declared after the transform system, so the joints' world transforms are this frame's.
  m_palette =
      ecs.system("SkinPalette").kind(world.phase(Phase::PreRender)).immediate().run([this](flecs::iter &) { pose(); });
  SONNET_LOG_DEBUG("animation ready");
}

AnimationSystem::~AnimationSystem() {
  m_palette.destruct();
  m_playback.destruct();
  m_skinned.destruct();
  m_animators.destruct();
}

bool AnimationSystem::valid(const Binding &binding, const core::Uuid &asset, std::uint64_t revision) const {
  if (binding.asset != asset || binding.revision != revision) {
    return false;
  }
  const flecs::world &ecs = m_world.ecs();
  return std::ranges::all_of(binding.targets,
                             [&](flecs::entity_t target) { return target == 0 || ecs.is_alive(target); });
}

void AnimationSystem::prune(std::unordered_map<flecs::entity_t, Binding> &bindings, std::uint64_t frame) {
  std::erase_if(bindings, [frame](const auto &entry) { return entry.second.frame != frame; });
}

void AnimationSystem::play(float dt) {
  SONNET_ZONE();
  flecs::world &ecs = m_world.ecs();
  ++m_playFrame;
  ecs.defer_suspend();
  m_entities.clear();
  m_animators.each([&](flecs::entity entity) { m_entities.push_back(entity); });
  for (const flecs::entity entity : m_entities) {
    const Animator *current = entity.try_get<Animator>();
    const assets::AnimationClip *clip = current != nullptr ? m_assets.animation(current->clip) : nullptr;
    if (clip == nullptr) {
      continue;
    }
    Animator animator = *current;
    advance(animator, clip->duration, dt);
    if (animator.playing || animator.time != current->time) {
      entity.set<Animator>(animator); // an instance gets its own from its prefab's here
    }

    Binding &binding = m_clipBindings[entity.id()];
    binding.frame = m_playFrame;
    if (!valid(binding, animator.clip, clip->revision)) {
      binding =
          Binding{.asset = animator.clip, .revision = clip->revision, .targets = {}, .order = {}, .frame = m_playFrame};
      std::vector<std::string> missing;
      for (const assets::AnimationChannel &channel : clip->channels) {
        const flecs::entity target = m_world.findByPath(entity, channel.target);
        binding.targets.push_back(target ? target.id() : 0);
        if (!target && std::ranges::find(missing, channel.target) == missing.end()) {
          missing.push_back(channel.target);
        }
      }
      binding.order.resize(clip->channels.size());
      for (std::uint32_t i = 0; i < binding.order.size(); ++i) {
        binding.order[i] = i;
      }
      std::ranges::stable_sort(binding.order, {}, [&](std::uint32_t i) { return binding.targets[i]; });
      if (!missing.empty()) {
        SONNET_LOG_WARN("{}: {} of the clip's nodes are not under it, the first \"{}\"", describe(entity),
                        missing.size(), missing.front());
      }
    }

    // Each target's channels are consecutive in `order`: its Transform is written once.
    flecs::entity_t written = 0;
    Transform transform;
    const auto flush = [&] {
      if (written != 0) {
        flecs::entity{ecs, written}.set<Transform>(transform);
      }
    };
    for (const std::uint32_t index : binding.order) {
      const flecs::entity_t target = binding.targets[index];
      if (target == 0 || !ecs.is_alive(target)) {
        continue;
      }
      if (target != written) {
        flush();
        written = target;
        const Transform *local = flecs::entity{ecs, target}.try_get<Transform>();
        transform = local != nullptr ? *local : Transform{};
      }
      const assets::AnimationChannel &channel = clip->channels[index];
      const glm::vec4 value = assets::sample(channel, animator.time);
      switch (channel.path) {
      case assets::AnimationPath::Translation:
        transform.position = glm::vec3{value};
        break;
      case assets::AnimationPath::Rotation:
        transform.rotation = glm::quat{value.w, value.x, value.y, value.z};
        break;
      case assets::AnimationPath::Scale:
        transform.scale = glm::vec3{value};
        break;
      }
    }
    flush();
  }
  ecs.defer_resume();
  prune(m_clipBindings, m_playFrame);
}

void AnimationSystem::pose() {
  SONNET_ZONE();
  flecs::world &ecs = m_world.ecs();
  ++m_poseFrame;
  ecs.defer_suspend();
  m_entities.clear();
  m_skinned.each([&](flecs::entity entity) { m_entities.push_back(entity); });
  for (const flecs::entity entity : m_entities) {
    const SkinnedMesh *skinned = entity.try_get<SkinnedMesh>();
    const assets::Skin *skin = skinned != nullptr ? m_assets.skin(skinned->skin) : nullptr;
    Binding &binding = m_skinBindings[entity.id()];
    binding.frame = m_poseFrame;
    if (skin == nullptr) {
      entity.remove<SkinPose>(); // drawn in its bind pose
      continue;
    }
    if (!valid(binding, skinned->skin, skin->revision)) {
      binding =
          Binding{.asset = skinned->skin, .revision = skin->revision, .targets = {}, .order = {}, .frame = m_poseFrame};
      // The nearest ancestor under which every joint resolves: a model instance's root.
      for (flecs::entity root = entity.parent(); root && binding.targets.empty(); root = root.parent()) {
        std::vector<flecs::entity_t> targets;
        for (const std::string &path : skin->joints) {
          const flecs::entity joint = m_world.findByPath(root, path);
          if (!joint) {
            break;
          }
          targets.push_back(joint.id());
        }
        if (targets.size() == skin->joints.size()) {
          binding.targets = std::move(targets);
        }
      }
      if (binding.targets.empty() && !skin->joints.empty()) {
        SONNET_LOG_WARN("{}: the skin's joints are not under any of its ancestors, drawn in the bind pose",
                        describe(entity));
        binding.targets.assign(skin->joints.size(), 0);
      }
    }
    if (std::ranges::find(binding.targets, flecs::entity_t{0}) != binding.targets.end() ||
        skin->inverseBindMatrices.size() != binding.targets.size()) {
      entity.remove<SkinPose>();
      continue;
    }
    const WorldTransform *meshWorld = entity.try_get<WorldTransform>();
    const glm::mat4 toMesh = glm::inverse(meshWorld != nullptr ? meshWorld->matrix : World::worldMatrix(entity));
    SkinPose &skinPose = entity.ensure<SkinPose>();
    skinPose.joints.resize(binding.targets.size());
    for (std::size_t i = 0; i < binding.targets.size(); ++i) {
      const flecs::entity joint{ecs, binding.targets[i]};
      const WorldTransform *jointWorld = joint.try_get<WorldTransform>();
      skinPose.joints[i] = toMesh * (jointWorld != nullptr ? jointWorld->matrix : World::worldMatrix(joint)) *
                           skin->inverseBindMatrices[i];
    }
  }
  ecs.defer_resume();
  prune(m_skinBindings, m_poseFrame);
}

} // namespace sonnet::world
