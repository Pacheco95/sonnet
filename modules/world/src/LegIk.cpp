#include <sonnet/world/LegIk.h>

#include <algorithm>
#include <cmath>

namespace sonnet::world {

namespace {

constexpr float Epsilon = 1.0e-6f;

// A unit vector at right angles to `v`, from the world axis `v` leans on least, so it is the same
// every time.
glm::vec3 anyPerpendicular(const glm::vec3 &v) {
  const glm::vec3 a = std::abs(v.x) <= std::abs(v.y) && std::abs(v.x) <= std::abs(v.z) ? glm::vec3{1.0f, 0.0f, 0.0f}
                      : std::abs(v.y) <= std::abs(v.z)                                 ? glm::vec3{0.0f, 1.0f, 0.0f}
                                                                                       : glm::vec3{0.0f, 0.0f, 1.0f};
  return glm::normalize(glm::cross(v, a));
}

// The shortest rotation from one unit vector to another.
glm::quat arc(const glm::vec3 &from, const glm::vec3 &to) {
  const float cosine = glm::dot(from, to);
  if (cosine > 0.999999f) {
    return glm::quat{1.0f, 0.0f, 0.0f, 0.0f};
  }
  if (cosine < -0.999999f) {
    return glm::angleAxis(glm::pi<float>(), anyPerpendicular(from));
  }
  const glm::vec3 axis = glm::cross(from, to);
  return glm::normalize(glm::quat{1.0f + cosine, axis.x, axis.y, axis.z});
}

// The identity turned by `weight` of the way to `rotation`, along the shorter arc.
glm::quat partOf(const glm::quat &rotation, float weight) {
  const glm::quat identity{1.0f, 0.0f, 0.0f, 0.0f};
  const glm::quat target = rotation.w < 0.0f ? -rotation : rotation;
  const float w = std::clamp(weight, 0.0f, 1.0f);
  const glm::quat mixed{identity.w + (target.w - identity.w) * w, target.x * w, target.y * w, target.z * w};
  return glm::normalize(mixed);
}

bool finite(const glm::vec3 &v) {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

} // namespace

TwoBone solveTwoBone(const glm::vec3 &hip, const glm::vec3 &knee, const glm::vec3 &ankle, const glm::vec3 &target,
                     const glm::vec3 &pole, float weight) {
  TwoBone result{.knee = knee, .ankle = ankle};
  if (!(weight > 0.0f) || !finite(hip) || !finite(knee) || !finite(ankle) || !finite(target) || !finite(pole)) {
    return result;
  }
  const glm::vec3 thigh = knee - hip;
  const glm::vec3 calf = ankle - knee;
  const float upperLength = glm::length(thigh);
  const float lowerLength = glm::length(calf);
  const glm::vec3 toTarget = target - hip;
  const float distance = glm::length(toTarget);
  if (!(upperLength > Epsilon) || !(lowerLength > Epsilon) || !(distance > Epsilon)) {
    return result;
  }

  const float length = upperLength + lowerLength;
  const float maxReach = 0.999f * length;
  const float minReach = std::abs(upperLength - lowerLength) + 0.001f * length;
  result.reached = distance <= maxReach;
  const float reach = std::clamp(distance, minReach, maxReach);
  const glm::vec3 axis = toTarget / distance;

  // The side the knee leans to: the pole's, at right angles to the axis.
  glm::vec3 side = pole - hip;
  side -= axis * glm::dot(side, axis);
  const float sideLength = glm::length(side);
  side = sideLength > 1.0e-5f * length ? side / sideLength : anyPerpendicular(axis);

  // The law of cosines: the angle at the hip between the axis and the thigh.
  const float cosine =
      std::clamp((upperLength * upperLength + reach * reach - lowerLength * lowerLength) / (2.0f * upperLength * reach),
                 -1.0f, 1.0f);
  const float sine = std::sqrt(std::max(0.0f, 1.0f - cosine * cosine));
  const glm::vec3 solvedKnee = hip + axis * (upperLength * cosine) + side * (upperLength * sine);
  const glm::vec3 solvedAnkle = hip + axis * reach;

  // The thigh swings to the new knee. The bend's plane before and after fixes the twist about it,
  // so the knee stays a hinge the way the clip bent it.
  const glm::vec3 thighDirection = thigh / upperLength;
  const glm::vec3 solvedThigh = glm::normalize(solvedKnee - hip);
  const glm::quat swing = arc(thighDirection, solvedThigh);
  glm::quat upper = swing;
  const glm::vec3 oldNormal = glm::cross(thigh, calf);
  const glm::vec3 newNormal = glm::cross(solvedKnee - hip, solvedAnkle - solvedKnee);
  const float oldLength = glm::length(oldNormal);
  const float newLength = glm::length(newNormal);
  if (oldLength > 1.0e-4f * upperLength * lowerLength && newLength > 1.0e-4f * upperLength * lowerLength) {
    const glm::vec3 from = swing * (oldNormal / oldLength);
    const glm::vec3 to = newNormal / newLength;
    const float angle = std::atan2(glm::dot(solvedThigh, glm::cross(from, to)), glm::dot(from, to));
    upper = glm::angleAxis(angle, solvedThigh) * swing;
  }

  // The calf, carried by the thigh, swings to the ankle.
  const glm::vec3 carried = glm::normalize(upper * calf);
  const glm::quat lower = arc(carried, glm::normalize(solvedAnkle - solvedKnee));

  result.upper = partOf(upper, weight);
  result.lower = partOf(lower, weight);
  result.knee = hip + result.upper * thigh;
  result.ankle = result.knee + result.lower * (result.upper * calf);
  return result;
}

glm::vec3 kneePole(const glm::vec3 &hip, const glm::vec3 &knee, const glm::vec3 &target, const glm::vec3 &forward) {
  const glm::vec3 toTarget = target - hip;
  const float distance = glm::length(toTarget);
  const glm::vec3 thigh = knee - hip;
  if (distance > Epsilon) {
    const glm::vec3 axis = toTarget / distance;
    const glm::vec3 off = thigh - axis * glm::dot(thigh, axis);
    // A knee within 1% of the thigh's length of the line is straight as far as a pole goes.
    if (glm::length(off) > 0.01f * glm::length(thigh)) {
      return knee;
    }
  }
  return hip + forward;
}

glm::quat footTilt(const glm::vec3 &up, const glm::vec3 &normal, float maxAngle, float weight) {
  const glm::quat identity{1.0f, 0.0f, 0.0f, 0.0f};
  const float length = glm::length(normal);
  if (!(weight > 0.0f) || !(length > Epsilon) || !std::isfinite(length)) {
    return identity;
  }
  const glm::vec3 to = normal / length;
  const float angle = std::acos(std::clamp(glm::dot(up, to), -1.0f, 1.0f));
  if (!(angle > Epsilon)) {
    return identity;
  }
  const glm::vec3 axis = glm::cross(up, to);
  const float axisLength = glm::length(axis);
  // Straight down has no shortest arc: any axis at right angles will do.
  const glm::vec3 about = axisLength > Epsilon ? axis / axisLength : anyPerpendicular(up);
  return glm::angleAxis(std::min(angle, std::max(maxAngle, 0.0f)) * std::clamp(weight, 0.0f, 1.0f), about);
}

} // namespace sonnet::world
