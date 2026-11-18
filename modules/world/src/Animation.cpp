#include <sonnet/world/Animation.h>

#include <sonnet/world/Components.h>
#include <sonnet/world/LegIk.h>

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
void advance(float &time, bool &playing, float speed, bool loop, float duration, float dt) {
  if (!playing) {
    return;
  }
  time += dt * speed;
  if (duration <= 0.0f) {
    time = 0.0f;
  } else if (loop) {
    time = std::fmod(time, duration);
    if (time < 0.0f) {
      time += duration;
    }
  } else if (time >= duration || time <= 0.0f) {
    time = std::clamp(time, 0.0f, duration);
    playing = speed == 0.0f;
  }
}

// A layer under this weight is fading out of earshot: its events are not worth a footstep.
constexpr float EventWeight = 0.05f;

// A model that moves further than this in a frame was teleported (the character sample's rule), so
// what leg IK had eased starts over there.
constexpr float TeleportDistance = 2.5f;

// What the pass counts as nothing: a weight, and a shift or a turn, small enough that rewriting the
// pose would only add rounding.
constexpr float IkMinWeight = 1.0e-4f;
constexpr float IkMinShift = 1.0e-5f;
constexpr float IkMinAngle = 1.0e-4f;

// The share of the way an exponential ease over `seconds` covers in `dt`: the same however the time
// is cut into frames, 0 for no time and 1 for no ease.
float easing(float dt, float seconds) {
  if (!(dt > 0.0f)) {
    return 0.0f;
  }
  if (!(seconds > 1.0e-6f)) {
    return 1.0f;
  }
  return 1.0f - std::exp(-dt / seconds);
}

float smoothStep(float low, float high, float x) {
  if (!(high > low)) {
    return x >= high ? 1.0f : 0.0f;
  }
  const float t = std::clamp((x - low) / (high - low), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

// A rotation of `rotation` about a point, as a world-space transform.
glm::mat4 turnAbout(const glm::quat &rotation, const glm::vec3 &pivot) {
  return glm::translate(glm::mat4{1.0f}, pivot) * glm::mat4_cast(rotation) * glm::translate(glm::mat4{1.0f}, -pivot);
}

} // namespace

AnimationSystem::AnimationSystem(World &world, assets::AssetDatabase &assets) : m_world(world), m_assets(assets) {
  flecs::world &ecs = world.ecs();
  m_animators = ecs.query_builder<>("Animators").with<Animator>().without<Disabled>().self().up(flecs::ChildOf).build();
  m_skinned =
      ecs.query_builder<>("SkinnedMeshes").with<SkinnedMesh>().without<Disabled>().self().up(flecs::ChildOf).build();
  // Immediate with deferring suspended, as the scripts' systems: the poses are written to
  // instances whose Transforms are inherited from their prefab until then.
  m_playback =
      ecs.system("AnimationPlayback").kind(world.phase(Phase::Update)).immediate().run([this](flecs::iter &it) {
        play(it.delta_time());
      });
  world.addToSimulation(m_playback);
  m_legged = ecs.query_builder<>("LegIks").with<LegIk>().without<Disabled>().self().up(flecs::ChildOf).build();
  // Declared after the transform system, so the joints' world transforms are this frame's, and
  // before the palette, which then builds from the world matrices this rewrites (ADR-0042). In edit
  // mode too: with a weight of 0, what a game that is not running leaves, it does nothing.
  m_legIk = ecs.system("LegIkPass").kind(world.phase(Phase::PreRender)).immediate().run([this](flecs::iter &it) {
    legIk(it.delta_time());
  });
  m_palette =
      ecs.system("SkinPalette").kind(world.phase(Phase::PreRender)).immediate().run([this](flecs::iter &) { pose(); });
  SONNET_LOG_DEBUG("animation ready");
}

AnimationSystem::~AnimationSystem() {
  m_palette.destruct();
  m_legIk.destruct();
  m_playback.destruct();
  m_legged.destruct();
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

void AnimationSystem::play(float dt) {
  SONNET_ZONE();
  flecs::world &ecs = m_world.ecs();
  ++m_playFrame;
  m_events.clear();
  ecs.defer_suspend();
  m_entities.clear();
  m_animators.each([&](flecs::entity entity) { m_entities.push_back(entity); });
  for (const flecs::entity entity : m_entities) {
    const Animator *current = entity.try_get<Animator>();
    if (current == nullptr) {
      continue;
    }
    Animator animator = *current;
    // Requested, so a clip whose file is not loaded yet imports off the main thread and plays
    // from a later frame instead of stalling the one that pressed play.
    const assets::AnimationClip *clip = m_assets.requestAnimation(animator.clip);

    // A clip assigned over another with a fade is a crossfade: the one it replaced carries on
    // from where it was, at the weight it had, losing it in the system's own list (not the
    // Animator's `layers`, which are the script's), while this one gains it.
    PlayState &state = m_states[entity.id()];
    state.frame = m_playFrame;
    if (state.known && state.clip != animator.clip) {
      if (animator.fade > 0.0f && !state.clip.isNil() && state.effective > 0.0f) {
        state.fading.push_back({.clip = state.clip,
                                .time = state.time,
                                .speed = state.speed,
                                .weight = state.effective,
                                .fadeRate = -state.effective / animator.fade,
                                .playing = true,
                                .loop = state.loop});
        state.ramp = 0.0f;
        state.fadeSeconds = animator.fade;
      } else {
        state.ramp = 1.0f;
      }
    }
    state.known = true;
    state.clip = animator.clip;
    if (state.ramp < 1.0f) {
      state.ramp = std::min(1.0f, state.ramp + dt / std::max(state.fadeSeconds, 1.0e-4f));
    }
    // Written as a negation so a NaN weight counts as none.
    const float weight = !(animator.weight > 0.0f) ? 0.0f : std::min(animator.weight, 1.0f);
    const float effective = weight * state.ramp;

    // Every clip that plays this frame, with the time it moved from.
    m_active.clear();
    PrimaryPhase phase;
    const float assigned = animator.time;
    if (clip != nullptr) {
      // A time outside the clip, from a script or a clip shorter than the last one, is brought
      // into it first, so the frame crosses only what it plays and no lap that never happened.
      if (clip->duration > 0.0f) {
        animator.time = animator.loop ? std::fmod(animator.time, clip->duration) : animator.time;
        if (animator.time < 0.0f && animator.loop) {
          animator.time += clip->duration;
        }
        animator.time = std::clamp(animator.time, 0.0f, clip->duration);
      }
      const float from = animator.time;
      advance(animator.time, animator.playing, animator.speed, animator.loop, clip->duration, dt);
      m_active.push_back({.clip = clip,
                          .from = from,
                          .to = animator.time,
                          .speed = animator.speed,
                          .weight = effective,
                          .loop = animator.loop,
                          .playing = animator.playing,
                          .slot = 0});
      if (clip->duration > 0.0f) {
        phase = {.phased = true,
                 .from = from / clip->duration,
                 .to = animator.time / clip->duration,
                 .speed = animator.speed,
                 .loop = animator.loop,
                 .playing = animator.playing};
      }
    }
    const bool hadLayers = !animator.layers.empty();
    for (std::size_t i = 0; i < animator.layers.size();) {
      if (!advanceLayer(animator.layers[i], static_cast<std::uint32_t>(i + 1), phase, dt)) {
        animator.layers.erase(animator.layers.begin() + static_cast<std::ptrdiff_t>(i));
        continue;
      }
      ++i;
    }
    for (std::size_t i = 0; i < state.fading.size();) {
      if (!advanceLayer(state.fading[i], FadingSlot + static_cast<std::uint32_t>(i), phase, dt)) {
        state.fading.erase(state.fading.begin() + static_cast<std::ptrdiff_t>(i));
        continue;
      }
      ++i;
    }
    state.time = animator.time;
    state.speed = animator.speed;
    state.loop = animator.loop;
    state.effective = effective;
    if (animator.playing || animator.time != assigned || hadLayers || !animator.layers.empty()) {
      entity.set<Animator>(animator); // an instance gets its own from its prefab's here
    }
    if (m_active.empty()) {
      continue;
    }

    // The blend: every layer's channels add their value, weighted, to the target they name.
    m_poses.clear();
    m_poseIndex.clear();
    for (const Active &active : m_active) {
      const core::Uuid &asset = active.slot == 0           ? animator.clip
                                : active.slot < FadingSlot ? animator.layers[active.slot - 1].clip
                                                           : state.fading[active.slot - FadingSlot].clip;
      const Binding &binding = bind(entity, active.slot, asset, *active.clip);
      for (std::size_t c = 0; c < active.clip->channels.size(); ++c) {
        const assets::AnimationChannel &channel = active.clip->channels[c];
        const flecs::entity_t target = binding.targets[c];
        if (target == 0 || !ecs.is_alive(target) || active.weight <= 0.0f) {
          continue;
        }
        const auto [slot, added] = m_poseIndex.try_emplace(target, static_cast<std::uint32_t>(m_poses.size()));
        if (added) {
          m_poses.push_back({.target = target});
        }
        TargetPose &pose = m_poses[slot->second];
        if (channel.path == assets::AnimationPath::Weights) {
          m_values.resize(assets::width(channel));
          assets::sample(channel, active.to, m_values);
          if (pose.morph.size() < m_values.size()) {
            pose.morph.resize(m_values.size(), 0.0f);
          }
          for (std::size_t w = 0; w < m_values.size(); ++w) {
            pose.morph[w] += m_values[w] * active.weight;
          }
          pose.morphWeight += active.weight;
          continue;
        }
        const glm::vec4 value = assets::sample(channel, active.to);
        switch (channel.path) {
        case assets::AnimationPath::Translation:
          pose.position += glm::vec3{value} * active.weight;
          pose.positionWeight += active.weight;
          break;
        case assets::AnimationPath::Scale:
          pose.scale += glm::vec3{value} * active.weight;
          pose.scaleWeight += active.weight;
          break;
        case assets::AnimationPath::Rotation: {
          // The shorter arc: a quaternion and its negative are the same turn.
          glm::vec4 q = value;
          if (pose.rotationWeight > 0.0f && glm::dot(q, pose.rotation) < 0.0f) {
            q = -q;
          }
          pose.rotation += q * active.weight;
          pose.rotationWeight += active.weight;
          break;
        }
        case assets::AnimationPath::Weights:
          break; // handled above
        }
      }
    }

    // Root motion: the root bone's translation leaves the pose, and what it moved this frame
    // moves the entity.
    if (animator.rootMotion && clip != nullptr && effective > 0.0f) {
      applyRootMotion(entity, animator, *clip, m_active.front().from, effective);
    }

    for (const TargetPose &pose : m_poses) {
      const flecs::entity target{ecs, pose.target};
      const Transform *local = target.try_get<Transform>();
      Transform transform = local != nullptr ? *local : Transform{};
      if (pose.positionWeight > 0.0f) {
        transform.position = pose.position / pose.positionWeight;
      }
      if (pose.scaleWeight > 0.0f) {
        transform.scale = pose.scale / pose.scaleWeight;
      }
      if (pose.rotationWeight > 0.0f) {
        const glm::vec4 q = pose.rotation;
        const float length = glm::length(q);
        transform.rotation = length > 0.0f ? glm::quat{q.w / length, q.x / length, q.y / length, q.z / length}
                                           : glm::quat{1.0f, 0.0f, 0.0f, 0.0f};
      }
      target.set<Transform>(transform);
      if (pose.morphWeight > 0.0f) {
        MorphWeights morph;
        if (const MorphWeights *existing = target.try_get<MorphWeights>(); existing != nullptr) {
          morph = *existing;
        }
        if (morph.weights.size() < pose.morph.size()) {
          morph.weights.resize(pose.morph.size(), 0.0f);
        }
        for (std::size_t w = 0; w < pose.morph.size(); ++w) {
          morph.weights[w] = pose.morph[w] / pose.morphWeight;
        }
        target.set<MorphWeights>(morph);
      }
    }

    for (const Active &active : m_active) {
      if (active.weight >= EventWeight && active.playing) {
        collectEvents(entity, active);
      }
    }
  }
  std::ranges::stable_sort(m_events, {}, &AnimationEventRecord::entity);
  ecs.defer_resume();
  prune(m_clipBindings, m_playFrame);
  prune(m_states, m_playFrame);
}

bool AnimationSystem::advanceLayer(AnimationLayer &layer, std::uint32_t slot, const PrimaryPhase &phase, float dt) {
  const assets::AnimationClip *clip = m_assets.requestAnimation(layer.clip);
  if (clip == nullptr) {
    return true; // waits for its file, or for a clip to be given
  }
  Active active{.clip = clip,
                .from = layer.time,
                .to = layer.time,
                .speed = layer.speed,
                .weight = 0.0f,
                .loop = layer.loop,
                .playing = layer.playing,
                .slot = slot};
  if (!layer.sync) {
    advance(layer.time, layer.playing, layer.speed, layer.loop, clip->duration, dt);
    active.to = layer.time;
    active.playing = layer.playing;
  } else if (phase.phased) {
    // The interval the Animator's clip crossed, mapped onto this one, with its direction and wrap.
    active.from = phase.from * clip->duration;
    active.to = phase.to * clip->duration;
    active.speed = phase.speed;
    active.loop = phase.loop;
    active.playing = phase.playing;
    layer.time = active.to;
  } else {
    // No phase to follow: it holds where it is and crosses nothing.
    active.speed = 0.0f;
    active.playing = false;
  }
  layer.weight += layer.fadeRate * dt;
  if (layer.fadeRate > 0.0f && layer.weight >= 1.0f) {
    layer.weight = 1.0f;
    layer.fadeRate = 0.0f;
  }
  if (layer.fadeRate < 0.0f && layer.weight <= 0.0f) {
    return false;
  }
  active.weight = layer.weight;
  m_active.push_back(active);
  return true;
}

const AnimationSystem::Binding &AnimationSystem::bind(flecs::entity entity, std::uint32_t slot, const core::Uuid &asset,
                                                      const assets::AnimationClip &clip) {
  Binding &binding = m_clipBindings[{entity.id(), slot}];
  binding.frame = m_playFrame;
  if (!valid(binding, asset, clip.revision)) {
    binding = Binding{.asset = asset, .revision = clip.revision, .targets = {}, .frame = m_playFrame};
    std::vector<std::string> missing;
    for (const assets::AnimationChannel &channel : clip.channels) {
      const flecs::entity target = m_world.findByPath(entity, channel.target);
      binding.targets.push_back(target ? target.id() : 0);
      if (!target && std::ranges::find(missing, channel.target) == missing.end()) {
        missing.push_back(channel.target);
      }
    }
    if (!missing.empty()) {
      SONNET_LOG_WARN("{}: {} of the clip's nodes are not under it, the first \"{}\"", describe(entity), missing.size(),
                      missing.front());
    }
  }
  return binding;
}

void AnimationSystem::applyRootMotion(flecs::entity entity, const Animator &animator, const assets::AnimationClip &clip,
                                      float from, float weight) {
  // The bone: the one named, or the translation channel nearest the root.
  const assets::AnimationChannel *root = nullptr;
  std::size_t rootIndex = 0;
  std::ptrdiff_t depth = 0;
  for (std::size_t c = 0; c < clip.channels.size(); ++c) {
    const assets::AnimationChannel &channel = clip.channels[c];
    if (channel.path != assets::AnimationPath::Translation) {
      continue;
    }
    if (!animator.rootBone.empty()) {
      if (channel.target == animator.rootBone) {
        root = &channel;
        rootIndex = c;
        break;
      }
      continue;
    }
    const std::ptrdiff_t channelDepth = std::ranges::count(channel.target, '/');
    if (root == nullptr || channelDepth < depth) {
      root = &channel;
      rootIndex = c;
      depth = channelDepth;
    }
  }
  if (root == nullptr) {
    return;
  }
  const auto at = [&](float time) { return glm::vec3{assets::sample(*root, time)}; };
  const float to = animator.time;
  glm::vec3 delta{0.0f};
  if (animator.playing && animator.speed != 0.0f) {
    const bool forward = animator.speed > 0.0f;
    const bool wrapped = animator.loop && (forward ? to < from : to > from);
    if (wrapped) {
      delta = forward ? (at(clip.duration) - at(from)) + (at(to) - at(0.0f))
                      : (at(0.0f) - at(from)) + (at(to) - at(clip.duration));
    } else {
      delta = at(to) - at(from);
    }
  }
  // Out of the pose: the bone stays where the clip starts.
  const auto found = m_poseIndex.find(m_clipBindings[{entity.id(), 0}].targets[rootIndex]);
  if (found != m_poseIndex.end()) {
    TargetPose &pose = m_poses[found->second];
    pose.position = at(0.0f);
    pose.positionWeight = 1.0f;
  }
  if (delta != glm::vec3{0.0f}) {
    const Transform *own = entity.try_get<Transform>();
    Transform transform = own != nullptr ? *own : Transform{};
    transform.position += transform.rotation * (delta * transform.scale) * weight;
    entity.set<Transform>(transform);
  }
}

void AnimationSystem::collectEvents(flecs::entity entity, const Active &active) {
  const float from = active.from;
  const float to = active.to;
  if (active.clip->events.empty() || active.speed == 0.0f || (from == to && !active.loop)) {
    return;
  }
  const bool forward = active.speed > 0.0f;
  const bool wrapped = active.loop && (forward ? to < from : to > from);
  for (const assets::AnimationEvent &event : active.clip->events) {
    const float t = event.time;
    bool crossed = false;
    if (forward) {
      crossed = wrapped ? (t > from || t <= to) : (t > from && t <= to);
    } else {
      crossed = wrapped ? (t < from || t >= to) : (t < from && t >= to);
    }
    if (crossed) {
      m_events.push_back({.entity = entity.id(), .time = t, .name = event.name, .argument = event.argument});
    }
  }
}

void AnimationSystem::legIk(float dt) {
  SONNET_ZONE();
  flecs::world &ecs = m_world.ecs();
  ++m_legFrame;
  ecs.defer_suspend();
  m_entities.clear();
  m_legged.each([&](flecs::entity entity) { m_entities.push_back(entity); });
  for (const flecs::entity entity : m_entities) {
    // A copy: the pass writes other entities' world transforms while it reads this.
    const LegIk *current = entity.try_get<LegIk>();
    if (current != nullptr) {
      const LegIk ik = *current;
      legIkOf(entity, ik, dt);
    }
  }
  ecs.defer_resume();
  prune(m_legStates, m_legFrame);
}

bool AnimationSystem::bindLegs(flecs::entity entity, const LegIk &ik, LegState &state) {
  const flecs::world &ecs = m_world.ecs();
  const auto matches = [&] {
    if (state.paths.size() != 1 + 3 * ik.legs.size() || state.legs.size() != ik.legs.size() ||
        state.paths[0] != ik.pelvis || (state.pelvis != 0 && !ecs.is_alive(state.pelvis))) {
      return false;
    }
    for (std::size_t i = 0; i < ik.legs.size(); ++i) {
      const LegBinding &leg = state.legs[i];
      if (state.paths[1 + 3 * i] != ik.legs[i].thigh || state.paths[2 + 3 * i] != ik.legs[i].calf ||
          state.paths[3 + 3 * i] != ik.legs[i].foot ||
          (leg.usable && (!ecs.is_alive(leg.thigh) || !ecs.is_alive(leg.calf) || !ecs.is_alive(leg.foot)))) {
        return false;
      }
    }
    return true;
  };
  if (matches()) {
    return state.pelvis != 0;
  }

  state.paths.clear();
  state.paths.push_back(ik.pelvis);
  for (const IkLeg &leg : ik.legs) {
    state.paths.insert(state.paths.end(), {leg.thigh, leg.calf, leg.foot});
  }
  state.known = false; // what eased is not for these bones
  state.legs.assign(ik.legs.size(), LegBinding{});
  const flecs::entity pelvis = ik.pelvis.empty() ? flecs::entity{} : m_world.findByPath(entity, ik.pelvis);
  state.pelvis = pelvis ? pelvis.id() : 0;
  if (!pelvis) {
    SONNET_LOG_WARN("{}: leg IK: the pelvis \"{}\" is not under it, no leg is moved", describe(entity), ik.pelvis);
    return false;
  }
  const auto find = [&](const std::string &path) {
    return path.empty() ? flecs::entity{} : m_world.findByPath(entity, path);
  };
  for (std::size_t i = 0; i < ik.legs.size(); ++i) {
    const IkLeg &source = ik.legs[i];
    const flecs::entity thigh = find(source.thigh);
    const flecs::entity calf = find(source.calf);
    const flecs::entity foot = find(source.foot);
    if (!thigh || !calf || !foot || !m_world.isDescendant(thigh, pelvis) || !m_world.isDescendant(calf, thigh) ||
        !m_world.isDescendant(foot, calf)) {
      SONNET_LOG_WARN(
          "{}: leg IK: leg {} (\"{}\", \"{}\", \"{}\") does not resolve to a thigh, a calf under it and a foot "
          "under that, below the pelvis; it is skipped",
          describe(entity), i, source.thigh, source.calf, source.foot);
      continue;
    }
    state.legs[i].thigh = thigh.id();
    state.legs[i].calf = calf.id();
    state.legs[i].foot = foot.id();
    state.legs[i].usable = true;
  }
  return true;
}

AnimationSystem::Rest AnimationSystem::restHeight(flecs::entity root, const std::string &foot, float &height) {
  bool waiting = false;
  bool found = false;
  m_skinned.each([&](flecs::entity mesh) {
    if (found || !m_world.isDescendant(mesh, root)) {
      return;
    }
    const SkinnedMesh *skinned = mesh.try_get<SkinnedMesh>();
    const assets::Skin *skin = skinned != nullptr ? m_assets.requestSkin(skinned->skin) : nullptr;
    if (skin == nullptr) {
      waiting = true;
      return;
    }
    const auto joint = std::ranges::find(skin->joints, foot);
    if (joint == skin->joints.end()) {
      return;
    }
    const auto index = static_cast<std::size_t>(joint - skin->joints.begin());
    if (index < skin->inverseBindMatrices.size()) {
      height = glm::inverse(skin->inverseBindMatrices[index])[3].y;
      found = true;
    }
  });
  return found ? Rest::Known : waiting ? Rest::Waiting : Rest::Missing;
}

// ADR-0042. The pass reads the joints' world matrices the transform system just wrote and the
// ground the game wrote into the component, eases what it keeps, and moves the pelvis subtree's
// world matrices; the local Transforms stay as the clips posed them.
void AnimationSystem::legIkOf(flecs::entity entity, const LegIk &ik, float dt) {
  flecs::world &ecs = m_world.ecs();
  LegState &state = m_legStates[entity.id()];
  state.frame = m_legFrame;
  const WorldTransform *rootWorld = entity.try_get<WorldTransform>();
  if (rootWorld == nullptr) {
    return;
  }
  const glm::vec3 origin{rootWorld->matrix[3]};
  if (!bindLegs(entity, ik, state)) {
    state.weight = 0.0f;
    return;
  }
  // The first frame, a rebound set of bones and a teleport take their values at once.
  const bool fresh = !state.known || glm::distance(origin, state.origin) > TeleportDistance;
  state.known = true;
  state.origin = origin;
  const float alpha = fresh ? 1.0f : easing(dt, ik.smoothing);
  const glm::vec3 up{0.0f, 1.0f, 0.0f};

  // Written as a negation so a NaN weight counts as none.
  const float wanted = !(ik.weight > 0.0f) ? 0.0f : std::min(ik.weight, 1.0f);
  state.weight += (wanted - state.weight) * alpha;

  m_legWork.clear();
  float dropWanted = 0.0f;
  for (std::size_t i = 0; i < ik.legs.size(); ++i) {
    LegBinding &leg = state.legs[i];
    const IkLeg &source = ik.legs[i];
    if (!leg.usable) {
      continue;
    }
    const WorldTransform *thigh = flecs::entity{ecs, leg.thigh}.try_get<WorldTransform>();
    const WorldTransform *calf = flecs::entity{ecs, leg.calf}.try_get<WorldTransform>();
    const WorldTransform *foot = flecs::entity{ecs, leg.foot}.try_get<WorldTransform>();
    if (thigh == nullptr || calf == nullptr || foot == nullptr) {
      continue;
    }
    LegWork work{.index = i,
                 .hip = glm::vec3{thigh->matrix[3]},
                 .knee = glm::vec3{calf->matrix[3]},
                 .ankle = glm::vec3{foot->matrix[3]}};

    // Planted: the animated ankle within `plantHeight` of its bind-pose height, off by `liftHeight`.
    if (!leg.restKnown && !leg.restMissing) {
      float height = 0.0f;
      switch (restHeight(entity, source.foot, height)) {
      case Rest::Known:
        leg.restHeight = height;
        leg.restKnown = true;
        break;
      case Rest::Missing:
        leg.restMissing = true;
        SONNET_LOG_WARN("{}: leg IK: no skin under it has \"{}\", so that foot counts as planted all the time",
                        describe(entity), source.foot);
        break;
      case Rest::Waiting:
        break;
      }
    }
    if (leg.restKnown) {
      const float lift = work.ankle.y - origin.y - leg.restHeight;
      work.contact = 1.0f - smoothStep(ik.plantHeight, ik.liftHeight, lift);
    }

    // The ground: what the game's query found, eased. A foot with no ground keeps what it had while
    // its trust fades, and one that comes back after being gone takes the new ground at once.
    const bool grounded =
        source.grounded && std::isfinite(source.groundPoint.y) && std::isfinite(glm::length(source.groundNormal));
    float rawOffset = leg.offset;
    if (grounded) {
      rawOffset = source.groundPoint.y - origin.y;
      const float normalLength = glm::length(source.groundNormal);
      const glm::vec3 normal = normalLength > 1.0e-4f ? source.groundNormal / normalLength : up;
      if (fresh || leg.ground < 0.01f) {
        leg.offset = rawOffset;
        leg.normal = normal;
      } else {
        leg.offset += (rawOffset - leg.offset) * alpha;
        const glm::vec3 eased = leg.normal + (normal - leg.normal) * alpha;
        const float easedLength = glm::length(eased);
        leg.normal = easedLength > 1.0e-4f ? eased / easedLength : up;
      }
    }
    leg.ground = fresh ? (grounded ? 1.0f : 0.0f) : leg.ground + ((grounded ? 1.0f : 0.0f) - leg.ground) * alpha;
    dropWanted = std::max(dropWanted, std::max(0.0f, -rawOffset) * work.contact * leg.ground);
    m_legWork.push_back(work);
  }
  const float dropLimit = ik.maxPelvisDrop > 0.0f ? ik.maxPelvisDrop : 0.0f;
  state.drop += (std::min(dropWanted, dropLimit) - state.drop) * alpha;

  if (!(state.weight >= IkMinWeight)) {
    state.weight = wanted > 0.0f ? state.weight : 0.0f;
    return;
  }

  // The pelvis drops by the lower foot's offset; it never rises, the raised foot bends instead.
  const float drop = state.weight * state.drop > IkMinShift ? state.weight * state.drop : 0.0f;
  const glm::mat4 dropped = glm::translate(glm::mat4{1.0f}, -up * drop);
  const glm::vec3 forward = glm::normalize(glm::vec3{rootWorld->matrix * glm::vec4{0.0f, 0.0f, -1.0f, 0.0f}});
  std::size_t kept = 0;
  for (LegWork &work : m_legWork) {
    const LegBinding &leg = state.legs[work.index];
    const glm::vec3 hip = work.hip - up * drop;
    const glm::vec3 knee = work.knee - up * drop;
    const glm::vec3 ankle = work.ankle - up * drop;
    // The foot asks for its ground's height above where the animation planted it, by how much it
    // is planted and trusted; ground the leg cannot reach, even with the pelvis dropped, is faded
    // out and the foot left as animated (the solver would only clamp it).
    const float planted = state.weight * work.contact * leg.ground;
    float offset = planted * leg.offset;
    const float length = glm::length(work.knee - work.hip) + glm::length(work.ankle - work.knee);
    const float distance = glm::distance(work.ankle + up * offset, hip);
    const float reach = 1.0f - smoothStep(0.999f * length, 1.04f * length, distance);
    offset *= reach;
    const glm::quat tilt = footTilt(up, leg.normal, ik.maxFootAngle, planted * reach);
    const bool shifted = drop > 0.0f || std::abs(offset) > IkMinShift;
    if (!shifted && !(glm::angle(tilt) > IkMinAngle)) {
      continue;
    }
    TwoBone solved{.knee = knee, .ankle = ankle};
    if (shifted) {
      const glm::vec3 target = work.ankle + up * offset;
      solved = solveTwoBone(hip, knee, ankle, target, kneePole(hip, knee, target, forward), 1.0f);
    }
    work.thigh = turnAbout(solved.upper, hip) * dropped;
    work.calf = turnAbout(solved.lower, solved.knee) * work.thigh;
    // The foot keeps the orientation the animation gave it in the world, not the shin's turn: it only
    // moves to the new ankle and then tilts to the ground, so a sole that was flat stays flat.
    work.foot = turnAbout(tilt, solved.ankle) * glm::translate(glm::mat4{1.0f}, solved.ankle - ankle) * dropped;
    m_legWork[kept++] = work;
  }
  m_legWork.resize(kept);
  if (drop == 0.0f && m_legWork.empty()) {
    return;
  }

  // The pelvis' subtree, parents first, then each node's move: the pelvis' drop, which every
  // node below inherits, and a leg's own bones on top of it.
  m_legNodes.clear();
  const flecs::entity pelvis{ecs, state.pelvis};
  const WorldTransform *pelvisWorld = pelvis.try_get<WorldTransform>();
  if (pelvisWorld == nullptr) {
    return;
  }
  m_legNodes.push_back({.entity = pelvis.id(), .parent = 0, .matrix = pelvisWorld->matrix});
  for (std::size_t i = 0; i < m_legNodes.size(); ++i) {
    flecs::entity{ecs, m_legNodes[i].entity}.children([&](flecs::entity child) {
      const WorldTransform *world = child.try_get<WorldTransform>();
      if (world != nullptr) {
        m_legNodes.push_back({.entity = child.id(), .parent = static_cast<std::uint32_t>(i), .matrix = world->matrix});
      }
    });
  }
  for (std::size_t i = 0; i < m_legNodes.size(); ++i) {
    LegNode &node = m_legNodes[i];
    if (i == 0) {
      node.delta = dropped;
      node.moved = drop > 0.0f;
    } else {
      node.delta = m_legNodes[node.parent].delta;
      node.moved = m_legNodes[node.parent].moved;
    }
    for (const LegWork &work : m_legWork) {
      const LegBinding &leg = state.legs[work.index];
      if (node.entity == leg.thigh) {
        node.delta = work.thigh;
      } else if (node.entity == leg.calf) {
        node.delta = work.calf;
      } else if (node.entity == leg.foot) {
        node.delta = work.foot;
      } else {
        continue;
      }
      node.moved = true;
    }
  }
  for (const LegNode &node : m_legNodes) {
    if (node.moved) {
      flecs::entity{ecs, node.entity}.set<WorldTransform>(WorldTransform{node.delta * node.matrix});
    }
  }
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
    // Requested: this runs every frame in the editor too, and a skin arrives with its mesh, so
    // until the file is in there is nothing to pose.
    const assets::Skin *skin = skinned != nullptr ? m_assets.requestSkin(skinned->skin) : nullptr;
    Binding &binding = m_skinBindings[entity.id()];
    binding.frame = m_poseFrame;
    if (skin == nullptr) {
      entity.remove<SkinPose>(); // drawn in its bind pose
      continue;
    }
    if (!valid(binding, skinned->skin, skin->revision)) {
      binding = Binding{.asset = skinned->skin, .revision = skin->revision, .targets = {}, .frame = m_poseFrame};
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
