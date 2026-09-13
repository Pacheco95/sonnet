#include <sonnet/world/Components.h>

namespace sonnet::world {

glm::mat4 Transform::matrix() const {
  return glm::translate(glm::mat4{1.0f}, position) * glm::mat4_cast(rotation) * glm::scale(glm::mat4{1.0f}, scale);
}

Transform Transform::fromMatrix(const glm::mat4 &matrix) {
  Transform result;
  result.position = glm::vec3{matrix[3]};
  result.scale = {glm::length(glm::vec3{matrix[0]}), glm::length(glm::vec3{matrix[1]}),
                  glm::length(glm::vec3{matrix[2]})};
  // A mirrored basis (negative determinant) keeps its handedness through a negative scale on
  // one axis; the rotation is then proper and quat_cast is well defined.
  if (glm::determinant(glm::mat3{matrix}) < 0.0f) {
    result.scale.x = -result.scale.x;
  }
  glm::mat3 rotation{matrix};
  for (int axis = 0; axis < 3; ++axis) {
    const float length = result.scale[axis];
    if (length != 0.0f) {
      rotation[axis] /= length;
    }
  }
  result.rotation = glm::normalize(glm::quat_cast(rotation));
  return result;
}

} // namespace sonnet::world
