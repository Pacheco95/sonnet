#include <sonnet/editor/FlyCamera.h>

#include <algorithm>
#include <cmath>

namespace sonnet::editor {

namespace {

constexpr glm::vec3 WorldUp{0.0f, 1.0f, 0.0f};
constexpr float PitchLimit = glm::radians(89.0f);
constexpr float FastMultiplier = 4.0f;
constexpr float DollyFraction = 0.1f;
constexpr float MinDollyStep = 0.05f;
constexpr float MinOrbitDistance = 0.05f;

} // namespace

FlyCamera::FlyCamera() {
  lookAt({5.0f, 3.5f, 7.0f}, {0.0f, 0.5f, 0.0f});
}

void FlyCamera::update(float dt, const Input &input) {
  turn(input.lookDelta);

  glm::vec3 direction{0.0f};
  if (input.forward) {
    direction += m_camera.forward();
  }
  if (input.back) {
    direction -= m_camera.forward();
  }
  if (input.right) {
    direction += m_camera.right();
  }
  if (input.left) {
    direction -= m_camera.right();
  }
  if (input.up) {
    direction += WorldUp;
  }
  if (input.down) {
    direction -= WorldUp;
  }
  if (glm::length(direction) > 0.0f) {
    const float speed = m_speed * (input.fast ? FastMultiplier : 1.0f);
    m_camera.position += glm::normalize(direction) * speed * dt;
  }
}

void FlyCamera::lookAt(glm::vec3 position, glm::vec3 target) {
  m_camera.position = position;
  const glm::vec3 direction = glm::normalize(target - position);
  m_yaw = std::atan2(-direction.x, -direction.z);
  m_pitch = std::clamp(std::asin(direction.y), -PitchLimit, PitchLimit);
  applyOrientation();
}

void FlyCamera::scaleSpeed(float factor) {
  m_speed = std::clamp(m_speed * factor, 0.1f, 200.0f);
}

void FlyCamera::dolly(float notches, float referenceDistance, bool fast) {
  const float step =
      referenceDistance > 0.0f ? std::max(referenceDistance * DollyFraction, MinDollyStep) : m_speed * 0.2f;
  m_camera.position += m_camera.forward() * (notches * step * (fast ? FastMultiplier : 1.0f));
}

void FlyCamera::orbit(glm::vec2 lookDelta, glm::vec3 pivot) {
  const float distance = std::max(glm::length(pivot - m_camera.position), MinOrbitDistance);
  turn(lookDelta);
  m_camera.position = pivot - m_camera.forward() * distance;
}

void FlyCamera::turn(glm::vec2 lookDelta) {
  m_yaw -= lookDelta.x * m_sensitivity;
  m_pitch = std::clamp(m_pitch - lookDelta.y * m_sensitivity, -PitchLimit, PitchLimit);
  applyOrientation();
}

void FlyCamera::applyOrientation() {
  // Yaw first around the world's up, then pitch around the resulting right axis, so the horizon
  // stays level.
  m_camera.rotation = glm::angleAxis(m_yaw, WorldUp) * glm::angleAxis(m_pitch, glm::vec3{1.0f, 0.0f, 0.0f});
}

} // namespace sonnet::editor
