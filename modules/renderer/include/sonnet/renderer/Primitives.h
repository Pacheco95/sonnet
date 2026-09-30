#pragma once

#include <sonnet/renderer/Mesh.h>

#include <cstdint>

namespace sonnet::renderer::primitives {

// Test geometry for scenes without assets. All are centred on the origin (the centre of their
// bounds), +Y up, and follow docs/conventions.md: metres, counter-clockwise front faces.

[[nodiscard]] MeshData box(glm::vec3 halfExtents = {0.5f, 0.5f, 0.5f});
[[nodiscard]] MeshData sphere(float radius = 0.5f, std::uint32_t slices = 32, std::uint32_t stacks = 16);
// A quad in the XZ plane facing +Y.
[[nodiscard]] MeshData plane(glm::vec2 size = {1.0f, 1.0f});
[[nodiscard]] MeshData cylinder(float radius = 0.5f, float height = 1.0f, std::uint32_t slices = 32);
// Total height includes the two hemispherical caps.
[[nodiscard]] MeshData capsule(float radius = 0.25f, float height = 1.0f, std::uint32_t slices = 32,
                               std::uint32_t rings = 8);
// A pointed cone with a flat base facing -Y.
[[nodiscard]] MeshData cone(float radius = 0.5f, float height = 1.0f, std::uint32_t slices = 32);
// A ring around the Y axis: `majorRadius` is the distance to the tube's centre, `minorRadius` the tube's.
[[nodiscard]] MeshData torus(float majorRadius = 0.35f, float minorRadius = 0.15f, std::uint32_t majorSegments = 32,
                             std::uint32_t minorSegments = 16);
// A wedge that rises towards -Z: the slope faces +Y and +Z, the tall end is a wall facing -Z.
[[nodiscard]] MeshData ramp(glm::vec3 size = {1.0f, 1.0f, 1.0f});
// Steps that rise towards -Z, `steps` treads over the depth of `size`.
[[nodiscard]] MeshData stairs(glm::vec3 size = {1.0f, 1.0f, 1.0f}, std::uint32_t steps = 4);
// The upper half of a sphere with a flat base facing -Y; bounds are `radius` wide and `radius` tall.
[[nodiscard]] MeshData hemisphere(float radius = 0.5f, std::uint32_t slices = 32, std::uint32_t rings = 8);
// A wall of the given size with a round-topped opening through its depth (Z). The opening is 60% of the
// width and its crown sits 0.4 widths below the top, so the height must be at least half the width.
[[nodiscard]] MeshData arch(glm::vec3 size = {1.0f, 1.0f, 0.25f}, std::uint32_t segments = 16);
// A sphere from a subdivided icosahedron; every triangle has about the same area, unlike the UV sphere's.
[[nodiscard]] MeshData icosphere(float radius = 0.5f, std::uint32_t subdivisions = 2);

} // namespace sonnet::renderer::primitives
