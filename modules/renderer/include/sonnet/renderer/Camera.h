#pragma once

#include <sonnet/core/Math.h>

namespace sonnet::renderer {

// Reversed-Z perspective with an infinite far plane (docs/rendering.md): the near plane maps to
// depth 1 and infinity to 0, for GLM_FORCE_DEPTH_ZERO_TO_ONE clip space.
[[nodiscard]] glm::mat4 perspectiveReversedZ(float fovY, float aspect, float nearPlane);

// The direction of the ray through a point of a view drawn with `viewProjection`. The point is a
// fraction of the view's size from its top-left corner: (0.5, 0.5) is the centre.
[[nodiscard]] glm::vec3 rayDirection(const glm::mat4 &viewProjection, glm::vec2 fraction);

[[nodiscard]] glm::mat4 orthographicReversedZ(float height, float aspect, float nearPlane, float farPlane);

struct Ray {
  glm::vec3 origin;
  glm::vec3 direction;
};
// Starts on the near plane, valid for both projection kinds.
[[nodiscard]] Ray cameraRay(const glm::mat4 &viewProjection, glm::vec2 fraction);

enum class ProjectionKind {
  Perspective,
  Orthographic
};

// A view point. Right-handed, +Y up, looking down -Z in its own frame (docs/conventions.md).
struct Camera {
  glm::vec3 position{0.0f, 0.0f, 0.0f};
  glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
  float fovY{glm::radians(60.0f)};
  float nearPlane{0.1f};
  ProjectionKind projectionKind{ProjectionKind::Perspective};
  float orthoHeight{10.0f};
  float farPlane{500.0f};
  float exposure{1.0f};
  float bloomStrength{0.04f}; // fraction of the blurred HDR image, 0 to 1

  [[nodiscard]] glm::vec3 forward() const;
  [[nodiscard]] glm::vec3 right() const;
  [[nodiscard]] glm::vec3 up() const;
  [[nodiscard]] glm::mat4 view() const;
  [[nodiscard]] glm::mat4 projection(float aspect) const;
  [[nodiscard]] Ray ray(glm::vec2 fraction, float aspect) const;
  // The direction through a point of a view of this aspect, as `renderer::rayDirection` takes it; the
  // origin is available from ray().
  [[nodiscard]] glm::vec3 rayDirection(glm::vec2 fraction, float aspect) const;
};

} // namespace sonnet::renderer
