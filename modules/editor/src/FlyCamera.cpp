#include <sonnet/editor/FlyCamera.h>

#include <algorithm>
#include <cmath>

namespace sonnet::editor {

namespace {

constexpr glm::vec3 WorldUp{0.0f, 1.0f, 0.0f};
constexpr float PitchLimit = glm::radians(89.0f);
constexpr float FastMultiplier = 4.0f;

} // namespace

FlyCamera::FlyCamera() {
  lookAt({5.0f, 3.5f, 7.0f}, {0.0f, 0.5f, 0.0f});
}

void FlyCamera::update(float dt, const Input &input) {
  m_yaw -= input.lookDelta.x * m_sensitivity;
  m_pitch = std::clamp(m_pitch - input.lookDelta.y * m_sensitivity, -PitchLimit, PitchLimit);
  applyOrientation();

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

void FlyCamera::applyOrientation() {
  // Yaw first around the world's up, then pitch around the resulting right axis, so the horizon
  // stays level.
  m_camera.rotation = glm::angleAxis(m_yaw, WorldUp) * glm::angleAxis(m_pitch, glm::vec3{1.0f, 0.0f, 0.0f});
}

} // namespace sonnet::editor
