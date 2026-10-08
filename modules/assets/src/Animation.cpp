#include <sonnet/assets/Animation.h>

#include <algorithm>
#include <array>
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

std::uint32_t width(const AnimationChannel &channel) {
  switch (channel.path) {
  case AnimationPath::Translation:
  case AnimationPath::Scale:
    return 3;
  case AnimationPath::Rotation:
    return 4;
  case AnimationPath::Weights:
    return channel.weightCount;
  }
  return 0;
}

void sample(const AnimationChannel &channel, float time, std::span<float> out) {
  std::ranges::fill(out, 0.0f);
  const std::size_t keys = channel.times.size();
  const std::size_t n = width(channel);
  const std::size_t stride = channel.interpolation == Interpolation::CubicSpline ? 3 : 1;
  if (keys == 0 || n == 0 || out.size() < n || channel.values.size() < keys * stride * n) {
    return;
  }
  // The value of key k, component c: the middle of a cubic spline's three.
  const auto at = [&](std::size_t k, std::size_t c) {
    return channel.values[(k * stride + (stride == 3 ? 1 : 0)) * n + c];
  };
  const bool rotation = channel.path == AnimationPath::Rotation;
  const auto vec = [&](std::size_t k) { return glm::vec4{at(k, 0), at(k, 1), at(k, 2), rotation ? at(k, 3) : 0.0f}; };
  const auto finishRotation = [&](glm::vec4 v) {
    const glm::vec4 q = fromQuat(glm::normalize(toQuat(v)));
    std::copy_n(&q.x, 4, out.begin());
  };
  const auto hold = [&](std::size_t k) {
    if (rotation) {
      finishRotation(vec(k));
      return;
    }
    for (std::size_t c = 0; c < n; ++c) {
      out[c] = at(k, c);
    }
  };
  if (keys == 1 || time <= channel.times.front()) {
    hold(0);
    return;
  }
  if (time >= channel.times.back()) {
    hold(keys - 1);
    return;
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
    hold(k);
    return;
  case Interpolation::Linear:
    if (rotation) {
      finishRotation(fromQuat(glm::slerp(toQuat(vec(k)), toQuat(vec(k + 1)), t)));
      return;
    }
    for (std::size_t c = 0; c < n; ++c) {
      out[c] = glm::mix(at(k, c), at(k + 1, c), t);
    }
    return;
  case Interpolation::CubicSpline: {
    // glTF's Hermite spline: the tangents are per second, scaled by the interval.
    const float t2 = t * t;
    const float t3 = t2 * t;
    const auto component = [&](std::size_t c) {
      const float outTangent = channel.values[(k * 3 + 2) * n + c] * span;
      const float inTangent = channel.values[((k + 1) * 3) * n + c] * span;
      return (2.0f * t3 - 3.0f * t2 + 1.0f) * at(k, c) + (t3 - 2.0f * t2 + t) * outTangent +
             (-2.0f * t3 + 3.0f * t2) * at(k + 1, c) + (t3 - t2) * inTangent;
    };
    if (rotation) {
      finishRotation({component(0), component(1), component(2), component(3)});
      return;
    }
    for (std::size_t c = 0; c < n; ++c) {
      out[c] = component(c);
    }
    return;
  }
  }
  hold(k);
}

glm::vec4 sample(const AnimationChannel &channel, float time) {
  std::array<float, 4> out{};
  if (channel.path == AnimationPath::Weights) {
    return glm::vec4{0.0f};
  }
  sample(channel, time, out);
  return {out[0], out[1], out[2], out[3]};
}

} // namespace sonnet::assets
