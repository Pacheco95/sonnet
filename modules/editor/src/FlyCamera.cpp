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
  const bool ortho = m_camera.projectionKind == renderer::ProjectionKind::Orthographic;

  glm::vec3 direction{0.0f};
  if (input.forward) {
    direction += ortho ? m_camera.up() : m_camera.forward();
  }
  if (input.back) {
    direction -= ortho ? m_camera.up() : m_camera.forward();
  }
  if (input.right) {
    direction += m_camera.right();
  }
  if (input.left) {
    direction -= m_camera.right();
  }
  if (input.up) {
    direction += ortho ? m_camera.up() : WorldUp;
  }
  if (input.down) {
    direction -= ortho ? m_camera.up() : WorldUp;
  }
  if (glm::length(direction) > 0.0f) {
    const float speed = m_speed * (input.fast ? FastMultiplier : 1.0f);
    m_camera.position += glm::normalize(direction) * speed * dt;
  }
}

void FlyCamera::lookAt(glm::vec3 position, glm::vec3 target) {
  m_axisView = AxisView::User;
  m_camera.position = position;
  const glm::vec3 direction = glm::normalize(target - position);
  m_yaw = std::atan2(-direction.x, -direction.z);
  m_pitch = std::clamp(std::asin(direction.y), -PitchLimit, PitchLimit);
  applyOrientation();
}

void FlyCamera::frame(glm::vec3 target, float distance) {
  m_camera.position = target - m_camera.forward() * distance;
  m_projectionDistance = distance;
  m_camera.orthoHeight = 2.0f * distance * std::tan(m_camera.fovY * 0.5f);
}

void FlyCamera::snap(AxisView view, glm::vec3 pivot) {
  if (view == AxisView::User) {
    return;
  }
  const float distance = std::max(glm::distance(m_camera.position, pivot), MinOrbitDistance);
  if (m_camera.projectionKind == renderer::ProjectionKind::Perspective) {
    m_camera.orthoHeight = 2.0f * distance * std::tan(m_camera.fovY * 0.5f);
    m_autoPerspective = true;
  }
  m_projectionDistance = distance;
  m_camera.projectionKind = renderer::ProjectionKind::Orthographic;
  m_axisView = view;
  m_yaw = view == AxisView::Back    ? glm::pi<float>()
          : view == AxisView::Right ? glm::half_pi<float>()
          : view == AxisView::Left  ? -glm::half_pi<float>()
                                    : 0.0f;
  m_pitch = view == AxisView::Top ? -glm::half_pi<float>() : view == AxisView::Bottom ? glm::half_pi<float>() : 0.0f;
  applyOrientation();
  m_camera.position = pivot - m_camera.forward() * distance;
}

void FlyCamera::opposite(glm::vec3 pivot) {
  switch (m_axisView) {
  case AxisView::Front:
    snap(AxisView::Back, pivot);
    break;
  case AxisView::Back:
    snap(AxisView::Front, pivot);
    break;
  case AxisView::Right:
    snap(AxisView::Left, pivot);
    break;
  case AxisView::Left:
    snap(AxisView::Right, pivot);
    break;
  case AxisView::Top:
    snap(AxisView::Bottom, pivot);
    break;
  case AxisView::Bottom:
    snap(AxisView::Top, pivot);
    break;
  case AxisView::User:
    break;
  }
}

void FlyCamera::toggleProjection(glm::vec3 pivot) {
  m_autoPerspective = false;
  if (m_camera.projectionKind == renderer::ProjectionKind::Perspective) {
    m_projectionDistance = std::max(glm::distance(m_camera.position, pivot), MinOrbitDistance);
    m_camera.orthoHeight = 2.0f * m_projectionDistance * std::tan(m_camera.fovY * 0.5f);
    m_camera.projectionKind = renderer::ProjectionKind::Orthographic;
  } else {
    const float distance = m_camera.orthoHeight / (2.0f * std::tan(m_camera.fovY * 0.5f));
    m_camera.position += m_camera.forward() * (glm::dot(pivot - m_camera.position, m_camera.forward()) - distance);
    m_camera.projectionKind = renderer::ProjectionKind::Perspective;
  }
}

void FlyCamera::zoom(float notches, glm::vec2 fraction, float aspect) {
  const glm::vec3 before = m_camera.ray(fraction, aspect).origin;
  m_camera.orthoHeight = std::clamp(m_camera.orthoHeight * std::pow(1.2f, -notches), 0.01f, 10000.0f);
  m_camera.position += before - m_camera.ray(fraction, aspect).origin;
}

const char *FlyCamera::viewName() const noexcept {
  switch (m_axisView) {
  case AxisView::Front:
    return "Front";
  case AxisView::Back:
    return "Back";
  case AxisView::Right:
    return "Right";
  case AxisView::Left:
    return "Left";
  case AxisView::Top:
    return "Top";
  case AxisView::Bottom:
    return "Bottom";
  case AxisView::User:
    return "User";
  }
  return "User";
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
  turn(lookDelta);
  const float distance = std::max(glm::length(pivot - m_camera.position), MinOrbitDistance);
  m_camera.position = pivot - m_camera.forward() * distance;
}

void FlyCamera::turn(glm::vec2 lookDelta) {
  if (lookDelta == glm::vec2{0.0f}) {
    return;
  }
  if (m_autoPerspective) {
    const glm::vec3 pivot = m_camera.position + m_camera.forward() * m_projectionDistance;
    toggleProjection(pivot);
  }
  m_axisView = AxisView::User;
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
