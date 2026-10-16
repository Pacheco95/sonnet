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

void FlyCamera::transitionFrom(const FlyCamera &from, glm::vec3 fromPivot, glm::vec3 toPivot, float seconds) {
  m_transitionDuration = std::max(seconds, 0.0f);
  m_transitionElapsed = 0.0f;
  if (!transitioning()) {
    return;
  }
  m_transition = {from.camera(), m_camera, from.dollyDepthOffset(), m_dollyDepthOffset, fromPivot, toPivot};
  m_dollyDepthOffset = from.dollyDepthOffset();
  m_camera = from.camera();
}

void FlyCamera::finishTransition() {
  if (transitioning()) {
    m_camera = m_transition.to;
    m_dollyDepthOffset = m_transition.toDollyOffset;
  }
  m_transitionDuration = 0.0f;
}

void FlyCamera::cancelTransition() {
  if (!transitioning()) {
    return;
  }
  m_transitionDuration = 0.0f;
  m_axisView = AxisView::User;
  m_autoPerspective = false;
  const glm::vec3 direction = m_camera.forward();
  m_yaw = std::atan2(-direction.x, -direction.z);
  m_pitch = std::asin(std::clamp(direction.y, -1.0f, 1.0f));
  const glm::quat level = glm::angleAxis(m_yaw, WorldUp) * glm::angleAxis(m_pitch, glm::vec3{1, 0, 0});
  // Slerp can introduce roll; retain it when input takes over rather than levelling abruptly.
  m_orientationOffset = glm::inverse(level) * m_camera.rotation;
}

glm::vec3 FlyCamera::transitionPivot() const {
  const float t =
      m_transitionDuration > 0.0f ? std::clamp(m_transitionElapsed / m_transitionDuration, 0.0f, 1.0f) : 1.0f;
  return glm::mix(m_transition.fromPivot, m_transition.toPivot, t * t * (3.0f - 2.0f * t));
}

void FlyCamera::advanceTransition(float dt) {
  if (!transitioning()) {
    return;
  }
  m_transitionElapsed += std::max(dt, 0.0f);
  if (m_transitionElapsed >= m_transitionDuration) {
    finishTransition();
    return;
  }
  const float t = m_transitionElapsed / m_transitionDuration;
  const float ease = t * t * (3.0f - 2.0f * t);
  const auto &from = m_transition.from;
  const auto &to = m_transition.to;
  const glm::vec3 pivot = glm::mix(m_transition.fromPivot, m_transition.toPivot, ease);
  m_camera.rotation = glm::normalize(glm::slerp(from.rotation, to.rotation, ease));
  float distance = glm::mix(glm::distance(from.position, m_transition.fromPivot),
                            glm::distance(to.position, m_transition.toPivot), ease);
  const auto height = [](const renderer::Camera &camera, glm::vec3 center) {
    return camera.projectionKind == renderer::ProjectionKind::Orthographic
               ? camera.orthoHeight
               : 2.0f * glm::distance(camera.position, center) * std::tan(camera.fovY * 0.5f);
  };
  m_camera.orthoHeight = glm::mix(height(from, m_transition.fromPivot), height(to, m_transition.toPivot), ease);
  if (from.projectionKind != to.projectionKind) {
    // Dolly zoom towards a nearly parallel perspective. At the endpoint the pivot plane
    // has exactly the orthographic size; the remaining perspective tends to zero.
    constexpr float ParallelFov = glm::radians(0.1f);
    const float startFov = from.projectionKind == renderer::ProjectionKind::Perspective ? from.fovY : ParallelFov;
    const float endFov = to.projectionKind == renderer::ProjectionKind::Perspective ? to.fovY : ParallelFov;
    m_camera.fovY = glm::mix(startFov, endFov, ease);
    m_camera.projectionKind = renderer::ProjectionKind::Perspective;
    distance = m_camera.orthoHeight / (2.0f * std::tan(m_camera.fovY * 0.5f));
  } else {
    m_camera.fovY = glm::mix(from.fovY, to.fovY, ease);
  }
  const float baseDistance =
      glm::mix(glm::distance(from.position, m_transition.fromPivot) - m_transition.fromDollyOffset,
               glm::distance(to.position, m_transition.toPivot) - m_transition.toDollyOffset, ease);
  m_dollyDepthOffset = std::max(distance - baseDistance, 0.0f);
  m_camera.position = pivot - m_camera.forward() * distance;
}

void FlyCamera::rebaseDolly(glm::vec3 pivot) {
  if (m_dollyDepthOffset <= 0.0f) {
    return;
  }
  const float distance = glm::distance(m_camera.position, pivot);
  const float baseDistance = std::max(distance - m_dollyDepthOffset, MinOrbitDistance);
  const float height = 2.0f * distance * std::tan(m_camera.fovY * 0.5f);
  m_camera.position = pivot - m_camera.forward() * baseDistance;
  m_camera.fovY = 2.0f * std::atan(height / (2.0f * baseDistance));
  m_dollyDepthOffset = 0.0f;
}

void FlyCamera::update(float dt, const Input &input) {
  if (input.lookDelta != glm::vec2{0.0f} || input.forward || input.back || input.left || input.right || input.up ||
      input.down) {
    cancelTransition();
  }
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
  cancelTransition();
  if (m_dollyDepthOffset > 0.0f) {
    m_camera.fovY = glm::radians(60.0f);
    m_dollyDepthOffset = 0.0f;
  }
  m_orientationOffset = {1, 0, 0, 0};
  m_axisView = AxisView::User;
  m_camera.position = position;
  const glm::vec3 direction = glm::normalize(target - position);
  m_yaw = std::atan2(-direction.x, -direction.z);
  m_pitch = std::clamp(std::asin(direction.y), -PitchLimit, PitchLimit);
  applyOrientation();
}

void FlyCamera::frame(glm::vec3 target, float distance) {
  cancelTransition();
  rebaseDolly(target);
  m_camera.position = target - m_camera.forward() * distance;
  m_projectionDistance = distance;
  m_camera.orthoHeight = 2.0f * distance * std::tan(m_camera.fovY * 0.5f);
}

void FlyCamera::snap(AxisView view, glm::vec3 pivot) {
  if (view == AxisView::User) {
    return;
  }
  cancelTransition();
  rebaseDolly(pivot);
  m_orientationOffset = {1, 0, 0, 0};
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
  cancelTransition();
  rebaseDolly(pivot);
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
  cancelTransition();
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
  cancelTransition();
  m_speed = std::clamp(m_speed * factor, 0.1f, 200.0f);
}

void FlyCamera::dolly(float notches, float referenceDistance, bool fast) {
  cancelTransition();
  const float step =
      referenceDistance > 0.0f ? std::max(referenceDistance * DollyFraction, MinDollyStep) : m_speed * 0.2f;
  m_camera.position += m_camera.forward() * (notches * step * (fast ? FastMultiplier : 1.0f));
}

void FlyCamera::orbit(glm::vec2 lookDelta, glm::vec3 pivot) {
  cancelTransition();
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
  // A cancelled snap can be closer to a pole than the normal fly limit. Let input
  // move away from it without first snapping a degree back towards the horizon.
  const float limit = std::max(PitchLimit, std::abs(m_pitch));
  m_pitch = std::clamp(m_pitch - lookDelta.y * m_sensitivity, -limit, limit);
  applyOrientation();
}

void FlyCamera::applyOrientation() {
  // Yaw first around the world's up, then pitch around the resulting right axis, so the horizon
  // stays level.
  m_camera.rotation =
      glm::angleAxis(m_yaw, WorldUp) * glm::angleAxis(m_pitch, glm::vec3{1.0f, 0.0f, 0.0f}) * m_orientationOffset;
}

} // namespace sonnet::editor
