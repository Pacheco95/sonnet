#pragma once

#include <sonnet/core/Math.h>

#include <cstdint>
#include <span>
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
  Weights, // a mesh's morph target weights (ADR-0023)
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
  // `width` floats per value: xyz for translation and scale, a quaternion's xyzw for rotation,
  // one per morph target for weights. A cubic spline has three values per key: the in-tangent, the
  // value and the out-tangent.
  std::uint32_t weightCount{0}; // the width of a Weights channel; the other paths have a fixed one
  std::vector<float> values;
};

// The floats in one value of the channel.
[[nodiscard]] std::uint32_t width(const AnimationChannel &channel);

// A named moment of a clip that scripts hear about (ADR-0023).
struct AnimationEvent {
  float time{0.0f}; // seconds
  std::string name;
  std::string argument;
};

struct AnimationClip {
  std::vector<AnimationChannel> channels;
  std::vector<AnimationEvent> events; // by time
  float duration{0.0f};               // the last key of any channel
  std::uint64_t revision{0};
};

// Writes the channel's value at `time` into `out`, which holds `width(channel)` floats: held at
// the first and last keys outside them, rotations normalised. A channel without keys is zero.
void sample(const AnimationChannel &channel, float time, std::span<float> out);

// The same for the translation, rotation and scale channels, as a vector (w is zero for the
// first and last).
[[nodiscard]] glm::vec4 sample(const AnimationChannel &channel, float time);

} // namespace sonnet::assets
