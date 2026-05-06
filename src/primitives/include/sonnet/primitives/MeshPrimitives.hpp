#pragma once

#include <sonnet/renderer/CPUMesh.hpp>

#include <glm/glm.hpp>

namespace sonnet::primitives {

// 6 faces × 4 verts each = 24 verts, flat normals per face.
[[nodiscard]] sonnet::renderer::CPUMesh makeBox(glm::vec3 size);

// Smooth normals. segX and segY clamped to minimum 3 and 2 respectively.
[[nodiscard]] sonnet::renderer::CPUMesh makeUVSphere(int segX, int segY);

// Flat-normal caps, smooth radial normals on the side.
[[nodiscard]] sonnet::renderer::CPUMesh makeCylinder(float radius, float height, int segments);

// Quad facing +Y, normal (0, 1, 0).
[[nodiscard]] sonnet::renderer::CPUMesh makePlane(glm::vec2 size);

// Two hemispheres (split from UV sphere) + cylinder body.
[[nodiscard]] sonnet::renderer::CPUMesh makeCapsule(float radius, float height, int segments);

} // namespace sonnet::primitives
