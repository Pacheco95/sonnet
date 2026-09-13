#pragma once

#include <sonnet/renderer/Mesh.h>

#include <cstdint>

namespace sonnet::renderer::primitives {

// Test geometry for scenes without assets. All are centred on the origin, +Y up, and follow
// docs/conventions.md: metres, counter-clockwise front faces.

[[nodiscard]] MeshData box(glm::vec3 halfExtents = {0.5f, 0.5f, 0.5f});
[[nodiscard]] MeshData sphere(float radius = 0.5f, std::uint32_t slices = 32, std::uint32_t stacks = 16);
// A quad in the XZ plane facing +Y.
[[nodiscard]] MeshData plane(glm::vec2 size = {1.0f, 1.0f});
[[nodiscard]] MeshData cylinder(float radius = 0.5f, float height = 1.0f, std::uint32_t slices = 32);
// Total height includes the two hemispherical caps.
[[nodiscard]] MeshData capsule(float radius = 0.25f, float height = 1.0f, std::uint32_t slices = 32,
                               std::uint32_t rings = 8);

} // namespace sonnet::renderer::primitives
