#pragma once

#include <sonnet/renderer/Camera.h>
#include <sonnet/renderer/Mesh.h>

#include <sonnet/core/Math.h>

#include <span>

namespace sonnet::renderer {

// What the renderer draws. The list is built by the layer above (the editor's test scene now,
// `world` from M2); the renderer never queries where it came from (docs/architecture.md).
struct DrawItem {
  MeshHandle mesh;
  glm::mat4 transform{1.0f};
  glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
};

struct DirectionalLight {
  glm::vec3 direction{-0.4f, -1.0f, -0.3f}; // towards where the light travels
  glm::vec3 color{1.0f, 0.96f, 0.9f};
  float intensity{3.0f};
};

struct SceneView {
  Camera camera;
  DirectionalLight light;
  glm::vec3 ambient{0.06f, 0.07f, 0.09f};
  std::span<const DrawItem> draws;
};

} // namespace sonnet::renderer
