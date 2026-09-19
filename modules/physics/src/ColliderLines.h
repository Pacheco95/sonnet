#pragma once

#include <sonnet/core/Math.h>
#include <sonnet/renderer/Mesh.h>
#include <sonnet/renderer/SceneView.h>

#include <flecs.h>

#include <vector>

namespace sonnet::physics {

// Appends the outlines of an entity's colliders at `world` (its world matrix, scale included):
// box edges, three great circles per sphere, rings and side lines per capsule, and the triangle
// edges of a mesh collider's mesh when one is given.
void appendColliderLines(flecs::entity entity, const glm::mat4 &world, const renderer::MeshData *mesh, glm::vec4 color,
                         std::vector<renderer::DebugLine> &lines);

} // namespace sonnet::physics
