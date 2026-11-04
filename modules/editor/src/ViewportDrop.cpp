#include <sonnet/editor/ViewportDrop.h>

#include <sonnet/editor/Gizmo.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

namespace sonnet::editor {

DropAction dropAction(const world::World &world, const assets::AssetDatabase &assets, const core::Uuid &uuid) {
  if (const flecs::entity prefab = world.find(uuid); prefab && prefab.has(flecs::Prefab)) {
    return DropAction::Instance;
  }
  const assets::AssetInfo *info = assets.find(uuid);
  if (info == nullptr) {
    return DropAction::None;
  }
  switch (info->type) {
  case assets::AssetType::Mesh:
    return DropAction::Mesh;
  case assets::AssetType::Material:
    return DropAction::Material;
  case assets::AssetType::Script:
    return DropAction::Script;
  case assets::AssetType::Environment:
    return DropAction::Environment;
  default:
    return DropAction::None;
  }
}

namespace {

// Whether the ray crosses the box at a parameter in [0, limit).
bool crossesBounds(glm::vec3 origin, glm::vec3 direction, const renderer::Bounds &bounds, float limit) {
  float enter = 0.0f;
  float leave = limit;
  for (int axis = 0; axis < 3; ++axis) {
    if (std::abs(direction[axis]) < 1e-12f) {
      if (origin[axis] < bounds.min[axis] || origin[axis] > bounds.max[axis]) {
        return false;
      }
      continue;
    }
    float t0 = (bounds.min[axis] - origin[axis]) / direction[axis];
    float t1 = (bounds.max[axis] - origin[axis]) / direction[axis];
    if (t0 > t1) {
      std::swap(t0, t1);
    }
    enter = std::max(enter, t0);
    leave = std::min(leave, t1);
    if (enter > leave) {
      return false;
    }
  }
  return true;
}

// Möller–Trumbore, both faces: the ray's parameter at the triangle, if it crosses it ahead of the origin.
std::optional<float> triangleHit(glm::vec3 origin, glm::vec3 direction, glm::vec3 a, glm::vec3 b, glm::vec3 c) {
  const glm::vec3 edge1 = b - a;
  const glm::vec3 edge2 = c - a;
  const glm::vec3 p = glm::cross(direction, edge2);
  const float determinant = glm::dot(edge1, p);
  if (std::abs(determinant) < 1e-12f) {
    return std::nullopt; // parallel to the triangle, or a degenerate one
  }
  const float inverse = 1.0f / determinant;
  const glm::vec3 s = origin - a;
  const float u = glm::dot(s, p) * inverse;
  if (u < 0.0f || u > 1.0f) {
    return std::nullopt;
  }
  const glm::vec3 q = glm::cross(s, edge1);
  const float v = glm::dot(direction, q) * inverse;
  if (v < 0.0f || u + v > 1.0f) {
    return std::nullopt;
  }
  const float t = glm::dot(edge2, q) * inverse;
  if (t < 0.0f) {
    return std::nullopt;
  }
  return t;
}

} // namespace

std::optional<SceneHit> raycastScene(const world::World &world, assets::AssetDatabase &assets,
                                     const renderer::Ray &ray) {
  std::optional<SceneHit> best;
  float bestDistance = std::numeric_limits<float>::infinity();
  // The entities the draw list shows (modules/world/src/DrawList.cpp).
  const auto meshes = world.ecs()
                          .query_builder<const world::WorldTransform, const world::MeshRenderer>()
                          .without<world::Disabled>()
                          .self()
                          .up(flecs::ChildOf)
                          .build();
  meshes.each(
      [&](flecs::entity entity, const world::WorldTransform &transform, const world::MeshRenderer &meshRenderer) {
        if (!meshRenderer.visible) {
          return;
        }
        // Requested rather than loaded, so a mesh still importing is skipped instead of stalling the frame.
        const renderer::MeshHandle mesh = assets.requestMesh(meshRenderer.mesh);
        if (!mesh) {
          return;
        }
        const glm::mat3 linear{transform.matrix};
        if (std::abs(glm::determinant(linear)) < 1e-12f) {
          return; // scaled flat: nothing to hit
        }
        // An affine map keeps the ray's parameter, so distances compare across meshes in world units.
        const glm::mat4 inverse = glm::inverse(transform.matrix);
        const glm::vec3 origin{inverse * glm::vec4{ray.origin, 1.0f}};
        const glm::vec3 direction{inverse * glm::vec4{ray.direction, 0.0f}};
        if (!crossesBounds(origin, direction, assets.renderer().meshBounds(mesh), bestDistance)) {
          return;
        }
        const renderer::MeshData *data = assets.meshData(meshRenderer.mesh);
        if (data == nullptr) {
          return;
        }
        std::optional<glm::vec3> localNormal;
        const std::size_t vertexCount = data->vertices.size();
        for (std::size_t i = 0; i + 2 < data->indices.size(); i += 3) {
          const std::uint32_t ia = data->indices[i];
          const std::uint32_t ib = data->indices[i + 1];
          const std::uint32_t ic = data->indices[i + 2];
          if (ia >= vertexCount || ib >= vertexCount || ic >= vertexCount) {
            continue;
          }
          const glm::vec3 a = data->vertices[ia].position;
          const glm::vec3 b = data->vertices[ib].position;
          const glm::vec3 c = data->vertices[ic].position;
          if (const std::optional<float> t = triangleHit(origin, direction, a, b, c); t && *t < bestDistance) {
            bestDistance = *t;
            localNormal = glm::cross(b - a, c - a);
          }
        }
        if (!localNormal) {
          return;
        }
        glm::vec3 normal = glm::normalize(glm::transpose(glm::inverse(linear)) * *localNormal);
        if (glm::dot(normal, ray.direction) > 0.0f) {
          normal = -normal;
        }
        best = SceneHit{.entity = entity, .point = ray.origin + ray.direction * bestDistance, .normal = normal};
      });
  return best;
}

Landing land(const renderer::Ray &ray, const std::optional<SceneHit> &hit, glm::vec3 gridNormal, float fallback,
             std::optional<float> snapStep) {
  Landing landing;
  if (hit) {
    landing = {.point = hit->point, .normal = hit->normal};
  } else if (const std::optional<glm::vec3> point =
                 Gizmo::planeRayHit(glm::vec3{0.0f}, gridNormal, ray.origin, ray.direction)) {
    landing = {.point = *point, .normal = glm::dot(gridNormal, ray.direction) > 0.0f ? -gridNormal : gridNormal};
  } else {
    landing = {.point = ray.origin + glm::normalize(ray.direction) * fallback, .normal = std::nullopt};
  }
  if (snapStep && *snapStep > 0.0f) {
    for (int axis = 0; axis < 3; ++axis) {
      if (std::abs(gridNormal[axis]) < 1e-6f) {
        landing.point[axis] = std::round(landing.point[axis] / *snapStep) * *snapStep;
      }
    }
  }
  return landing;
}

glm::vec3 restingPosition(const Landing &landing, const renderer::Renderer &renderer,
                          std::span<const renderer::DrawItem> items, const glm::mat4 &pose) {
  if (!landing.normal) {
    return landing.point;
  }
  const glm::vec3 normal = *landing.normal;
  std::optional<float> lowest;
  for (const renderer::DrawItem &item : items) {
    if (!renderer.isValid(item.mesh)) {
      continue;
    }
    const renderer::Bounds bounds = renderer.meshBounds(item.mesh);
    const glm::mat4 matrix = pose * item.transform;
    for (int corner = 0; corner < 8; ++corner) {
      const glm::vec3 local{(corner & 1) != 0 ? bounds.max.x : bounds.min.x,
                            (corner & 2) != 0 ? bounds.max.y : bounds.min.y,
                            (corner & 4) != 0 ? bounds.max.z : bounds.min.z};
      const float along = glm::dot(glm::vec3{matrix * glm::vec4{local, 1.0f}}, normal);
      lowest = lowest ? std::min(*lowest, along) : along;
    }
  }
  return lowest ? landing.point - normal * *lowest : landing.point;
}

} // namespace sonnet::editor
