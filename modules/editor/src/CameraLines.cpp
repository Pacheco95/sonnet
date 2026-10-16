#include <sonnet/editor/CameraLines.h>

#include <glm/glm.hpp>

#include <array>
#include <cmath>

namespace sonnet::editor {

namespace {

constexpr glm::vec4 Color{0.85f, 0.85f, 0.85f, 1.0f};
// tan(fovY / 2) is unbounded at a straight angle; the inspector cannot get there, a scene file can.
constexpr float MaxFovY = 3.0f;
constexpr float MinFovY = 0.01f;

// The corners of the view's cross-section `distance` in front of the camera, in order around it.
std::array<glm::vec3, 4> section(const renderer::Camera &camera, float aspect, float distance) {
  const float halfHeight = camera.projectionKind == renderer::ProjectionKind::Orthographic
                               ? camera.orthoHeight * 0.5f
                               : distance * std::tan(glm::clamp(camera.fovY, MinFovY, MaxFovY) * 0.5f);
  const glm::vec3 centre = camera.position + camera.forward() * distance;
  const glm::vec3 up = camera.up() * halfHeight;
  const glm::vec3 right = camera.right() * (halfHeight * aspect);
  return {centre - right - up, centre + right - up, centre + right + up, centre - right + up};
}

void rectangle(const std::array<glm::vec3, 4> &corners, std::vector<renderer::DebugLine> &lines) {
  for (std::size_t i = 0; i < corners.size(); ++i) {
    lines.push_back({.from = corners[i], .to = corners[(i + 1) % corners.size()], .color = Color});
  }
}

} // namespace

void appendCameraFrustumLines(std::span<const renderer::Camera> cameras, float aspect,
                              std::vector<renderer::DebugLine> &lines) {
  for (const renderer::Camera &camera : cameras) {
    const std::array<glm::vec3, 4> farCorners = section(camera, aspect, camera.nearPlane + CameraGizmoLength);
    const auto nearCorners = section(camera, aspect, camera.nearPlane);
    rectangle(nearCorners, lines);
    rectangle(farCorners, lines);
    for (std::size_t i = 0; i < farCorners.size(); ++i) {
      const auto origin =
          camera.projectionKind == renderer::ProjectionKind::Orthographic ? nearCorners[i] : camera.position;
      lines.push_back({.from = origin, .to = farCorners[i], .color = Color});
    }
  }
}

} // namespace sonnet::editor
