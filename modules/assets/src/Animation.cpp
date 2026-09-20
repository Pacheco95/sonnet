#include <sonnet/assets/Animation.h>

#include <algorithm>
#include <iterator>

namespace sonnet::assets {

namespace {

glm::quat toQuat(const glm::vec4 &v) {
  return glm::quat{v.w, v.x, v.y, v.z};
}

glm::vec4 fromQuat(const glm::quat &q) {
  return {q.x, q.y, q.z, q.w};
}

} // namespace

glm::vec4 sample(const AnimationChannel &channel, float time) {
  const std::size_t keys = channel.times.size();
  const std::size_t stride = channel.interpolation == Interpolation::CubicSpline ? 3 : 1;
  if (keys == 0 || channel.values.size() < keys * stride) {
    return glm::vec4{0.0f};
  }
  // The value of key k: the middle of a cubic spline's three.
  const auto value = [&](std::size_t k) { return channel.values[k * stride + (stride == 3 ? 1 : 0)]; };
  const bool rotation = channel.path == AnimationPath::Rotation;
  const auto finish = [&](glm::vec4 v) { return rotation ? fromQuat(glm::normalize(toQuat(v))) : v; };
  if (keys == 1 || time <= channel.times.front()) {
    return finish(value(0));
  }
  if (time >= channel.times.back()) {
    return finish(value(keys - 1));
  }
  // The key at or before `time`: the last whose time is not after it.
  const auto after = std::ranges::upper_bound(channel.times, time);
  const std::size_t k = static_cast<std::size_t>(std::distance(channel.times.begin(), after)) - 1;
  const float t0 = channel.times[k];
  const float t1 = channel.times[k + 1];
  const float span = t1 - t0;
  const float t = span > 0.0f ? (time - t0) / span : 0.0f;
  switch (channel.interpolation) {
  case Interpolation::Step:
    return finish(value(k));
  case Interpolation::Linear:
    if (rotation) {
      return fromQuat(glm::normalize(glm::slerp(toQuat(value(k)), toQuat(value(k + 1)), t)));
    }
    return glm::mix(value(k), value(k + 1), t);
  case Interpolation::CubicSpline: {
    // glTF's Hermite spline: the tangents are per second, scaled by the interval.
    const glm::vec4 outTangent = channel.values[k * 3 + 2] * span;
    const glm::vec4 inTangent = channel.values[(k + 1) * 3] * span;
    const float t2 = t * t;
    const float t3 = t2 * t;
    return finish((2.0f * t3 - 3.0f * t2 + 1.0f) * value(k) + (t3 - 2.0f * t2 + t) * outTangent +
                  (-2.0f * t3 + 3.0f * t2) * value(k + 1) + (t3 - t2) * inTangent);
  }
  }
  return finish(value(k));
}

} // namespace sonnet::assets
