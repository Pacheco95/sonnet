#include "ColliderLines.h"

#include <sonnet/physics/Components.h>

#include <array>
#include <cstddef>
#include <numbers>

namespace sonnet::physics {

namespace {

constexpr int CircleSegments = 24;

struct LineSink {
  const glm::mat4 &matrix;
  glm::vec4 color;
  std::vector<renderer::DebugLine> &lines;

  void add(glm::vec3 from, glm::vec3 to) const {
    lines.push_back({.from = glm::vec3{matrix * glm::vec4{from, 1.0f}},
                     .to = glm::vec3{matrix * glm::vec4{to, 1.0f}},
                     .color = color});
  }

  // An arc about `centre` in the plane of the unit axes `u` and `v`, from angle `start` over
  // `sweep` radians.
  void arc(glm::vec3 centre, glm::vec3 u, glm::vec3 v, float radius, float start, float sweep, int segments) const {
    glm::vec3 previous = centre + radius * (std::cos(start) * u + std::sin(start) * v);
    for (int i = 1; i <= segments; ++i) {
      const float angle = start + sweep * static_cast<float>(i) / static_cast<float>(segments);
      const glm::vec3 next = centre + radius * (std::cos(angle) * u + std::sin(angle) * v);
      add(previous, next);
      previous = next;
    }
  }
};

constexpr glm::vec3 X{1.0f, 0.0f, 0.0f};
constexpr glm::vec3 Y{0.0f, 1.0f, 0.0f};
constexpr glm::vec3 Z{0.0f, 0.0f, 1.0f};
constexpr float Tau = 2.0f * std::numbers::pi_v<float>;
constexpr float Pi = std::numbers::pi_v<float>;

} // namespace

void appendColliderLines(flecs::entity entity, const glm::mat4 &world, const renderer::MeshData *mesh, glm::vec4 color,
                         std::vector<renderer::DebugLine> &lines) {
  const LineSink sink{world, color, lines};
  if (const BoxCollider *box = entity.try_get<BoxCollider>()) {
    const glm::vec3 h = box->halfExtents;
    std::array<glm::vec3, 8> corners;
    for (std::size_t i = 0; i < corners.size(); ++i) {
      corners[i] =
          box->offset + glm::vec3{(i & 1) != 0 ? h.x : -h.x, (i & 2) != 0 ? h.y : -h.y, (i & 4) != 0 ? h.z : -h.z};
    }
    // Corners differing in one bit share an edge.
    for (std::size_t i = 0; i < corners.size(); ++i) {
      for (const std::size_t bit : {1u, 2u, 4u}) {
        if ((i & bit) == 0) {
          sink.add(corners[i], corners[i | bit]);
        }
      }
    }
  }
  if (const SphereCollider *sphere = entity.try_get<SphereCollider>()) {
    sink.arc(sphere->offset, X, Y, sphere->radius, 0.0f, Tau, CircleSegments);
    sink.arc(sphere->offset, Y, Z, sphere->radius, 0.0f, Tau, CircleSegments);
    sink.arc(sphere->offset, Z, X, sphere->radius, 0.0f, Tau, CircleSegments);
  }
  if (const CapsuleCollider *capsule = entity.try_get<CapsuleCollider>()) {
    const float r = capsule->radius;
    const glm::vec3 top = capsule->offset + Y * capsule->halfHeight;
    const glm::vec3 bottom = capsule->offset - Y * capsule->halfHeight;
    sink.arc(top, Z, X, r, 0.0f, Tau, CircleSegments);
    sink.arc(bottom, Z, X, r, 0.0f, Tau, CircleSegments);
    for (const glm::vec3 side : {X, -X, Z, -Z}) {
      sink.add(bottom + side * r, top + side * r);
    }
    // The caps as half circles in the two vertical planes.
    sink.arc(top, X, Y, r, 0.0f, Pi, CircleSegments / 2);
    sink.arc(top, Z, Y, r, 0.0f, Pi, CircleSegments / 2);
    sink.arc(bottom, X, Y, r, Pi, Pi, CircleSegments / 2);
    sink.arc(bottom, Z, Y, r, Pi, Pi, CircleSegments / 2);
  }
  if (mesh != nullptr && entity.has<MeshCollider>()) {
    for (std::size_t i = 0; i + 2 < mesh->indices.size(); i += 3) {
      const glm::vec3 a = mesh->vertices[mesh->indices[i]].position;
      const glm::vec3 b = mesh->vertices[mesh->indices[i + 1]].position;
      const glm::vec3 c = mesh->vertices[mesh->indices[i + 2]].position;
      sink.add(a, b);
      sink.add(b, c);
      sink.add(c, a);
    }
  }
}

} // namespace sonnet::physics
