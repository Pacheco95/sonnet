#pragma once

#include <sonnet/core/Math.h>

#include <cstdint>
#include <string>
#include <vector>

namespace sonnet::assets {

// Skins and animation clips, sub-assets of a glTF model (docs/assets.md, "Skins and
// animations"). Both name the nodes they act on by path: the names from the model's root to the
// node, joined by '/', which `world` resolves under a model's instance (ADR-0010).

// The joints a skinned mesh's weights index, in order.
struct Skin {
  std::vector<std::string> joints;
  // Per joint: from the mesh's space in the bind pose into the joint's.
  std::vector<glm::mat4> inverseBindMatrices;
  std::uint64_t revision{0}; // changes with every load, so bindings made from an older one rebuild
};

// 32-bit, the width reflection reads enum values with.
enum class AnimationPath : std::int32_t {
  Translation,
  Rotation,
  Scale,
};

enum class Interpolation : std::int32_t {
  Linear, // spherical for rotations
  Step,
  CubicSpline,
};

// One property of one node over time.
struct AnimationChannel {
  std::string target; // the node's path
  AnimationPath path{AnimationPath::Translation};
  Interpolation interpolation{Interpolation::Linear};
  std::vector<float> times; // seconds, increasing
  // xyz for translation and scale, a quaternion's xyzw for rotation; a cubic spline has three per
  // key: the in-tangent, the value and the out-tangent.
  std::vector<glm::vec4> values;
};

struct AnimationClip {
  std::vector<AnimationChannel> channels;
  float duration{0.0f}; // the last key of any channel
  std::uint64_t revision{0};
};

// The channel's value at `time`, held at the first and last keys outside them; rotations come
// back normalised. A channel without keys is zero.
[[nodiscard]] glm::vec4 sample(const AnimationChannel &channel, float time);

} // namespace sonnet::assets
