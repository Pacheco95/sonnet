#pragma once

#include <sonnet/core/Math.h>

namespace sonnet::world {

// The result of solveTwoBone: where a two-bone chain (thigh and calf, or any limb) ends up, and the
// world-space rotations that take it there.
struct TwoBone {
  // About the hip, applied to the upper bone and everything below it.
  glm::quat upper{1.0f, 0.0f, 0.0f, 0.0f};
  // About the new knee, applied after `upper` to the lower bone and everything below it.
  glm::quat lower{1.0f, 0.0f, 0.0f, 0.0f};
  glm::vec3 knee{0.0f};
  glm::vec3 ankle{0.0f};
  // False when the target was past the chain's length and the ankle stopped short of it.
  bool reached{true};
};

// An analytic two-bone solve (ADR-0042), in world space, for a chain whose joints are at `hip`,
// `knee` and `ankle` and whose ankle should be at `target`. The knee leans towards `pole`, a point:
// the plane of the chain is the one through the hip, the target and the pole. A target past the
// chain's length is clamped to 0.999 times it along the hip-to-target line, so the result stays
// finite and the knee never flips. `weight` blends the rotations with the input pose by a
// normalised lerp along the shorter arc; a weight of 0, or anything not above it, returns the input
// pose bit for bit. The knee is kept hinged: the upper bone is twisted so the chain bends in the
// new plane the way it bent in the old one.
[[nodiscard]] TwoBone solveTwoBone(const glm::vec3 &hip, const glm::vec3 &knee, const glm::vec3 &ankle,
                                   const glm::vec3 &target, const glm::vec3 &pole, float weight);

// The pole for a leg: the animated knee, which already bends the way the clip meant, or, when the
// leg is straight along the hip-to-target line, a point `forward` of the hip.
[[nodiscard]] glm::vec3 kneePole(const glm::vec3 &hip, const glm::vec3 &knee, const glm::vec3 &target,
                                 const glm::vec3 &forward);

// The world-space rotation, about the ankle, that turns the foot from standing on level ground
// (`up`) to standing on ground with `normal`: the shortest arc, held to `maxAngle` and scaled by
// `weight`. Identity for a flat normal or a weight of 0.
[[nodiscard]] glm::quat footTilt(const glm::vec3 &up, const glm::vec3 &normal, float maxAngle, float weight);

} // namespace sonnet::world
