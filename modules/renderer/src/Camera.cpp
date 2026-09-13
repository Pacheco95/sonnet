#include <sonnet/renderer/Camera.h>

#include <cmath>

namespace sonnet::renderer {

glm::mat4 perspectiveReversedZ(float fovY, float aspect, float nearPlane) {
  const float f = 1.0f / std::tan(fovY * 0.5f);
  glm::mat4 projection{0.0f};
  projection[0][0] = f / aspect;
  projection[1][1] = f;
  // Column 2 maps view-space z to clip z = near and w = -z, so depth = near / -z: 1 at the near
  // plane, 0 at infinity.
  projection[2][3] = -1.0f;
  projection[3][2] = nearPlane;
  return projection;
}

glm::vec3 Camera::forward() const {
  return rotation * glm::vec3{0.0f, 0.0f, -1.0f};
}

glm::vec3 Camera::right() const {
  return rotation * glm::vec3{1.0f, 0.0f, 0.0f};
}

glm::vec3 Camera::up() const {
  return rotation * glm::vec3{0.0f, 1.0f, 0.0f};
}

glm::mat4 Camera::view() const {
  return glm::lookAt(position, position + forward(), up());
}

glm::mat4 Camera::projection(float aspect) const {
  return perspectiveReversedZ(fovY, aspect, nearPlane);
}

} // namespace sonnet::renderer
