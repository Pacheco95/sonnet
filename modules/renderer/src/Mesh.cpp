#include <sonnet/renderer/Mesh.h>

#include <algorithm>
#include <cmath>

namespace sonnet::renderer {

Bounds MeshData::bounds() const {
  if (vertices.empty()) {
    return {};
  }
  Bounds result{vertices[0].position, vertices[0].position};
  for (const Vertex &vertex : vertices) {
    result.min = glm::min(result.min, vertex.position);
    result.max = glm::max(result.max, vertex.position);
  }
  return result;
}

void generateTangents(MeshData &mesh) {
  std::vector<glm::vec3> tangents(mesh.vertices.size(), glm::vec3{0.0f});
  std::vector<glm::vec3> bitangents(mesh.vertices.size(), glm::vec3{0.0f});
  for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
    const std::uint32_t i0 = mesh.indices[t];
    const std::uint32_t i1 = mesh.indices[t + 1];
    const std::uint32_t i2 = mesh.indices[t + 2];
    const Vertex &v0 = mesh.vertices[i0];
    const Vertex &v1 = mesh.vertices[i1];
    const Vertex &v2 = mesh.vertices[i2];
    const glm::vec3 e1 = v1.position - v0.position;
    const glm::vec3 e2 = v2.position - v0.position;
    const glm::vec2 d1 = v1.uv - v0.uv;
    const glm::vec2 d2 = v2.uv - v0.uv;
    const float determinant = d1.x * d2.y - d2.x * d1.y;
    if (std::abs(determinant) < 1.0e-12f) {
      continue; // no uv area: the vertex keeps whatever its other triangles say
    }
    const float r = 1.0f / determinant;
    const glm::vec3 tangent = (e1 * d2.y - e2 * d1.y) * r;
    const glm::vec3 bitangent = (e2 * d1.x - e1 * d2.x) * r;
    for (const std::uint32_t index : {i0, i1, i2}) {
      tangents[index] += tangent;
      bitangents[index] += bitangent;
    }
  }
  for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
    Vertex &vertex = mesh.vertices[i];
    const glm::vec3 n = vertex.normal;
    glm::vec3 t = tangents[i] - n * glm::dot(n, tangents[i]);
    if (glm::dot(t, t) < 1.0e-12f) {
      // No usable uv derivative: any direction in the tangent plane will do.
      const glm::vec3 axis = std::abs(n.x) < 0.9f ? glm::vec3{1.0f, 0.0f, 0.0f} : glm::vec3{0.0f, 1.0f, 0.0f};
      t = glm::cross(axis, n);
    }
    t = glm::normalize(t);
    const float sign = glm::dot(glm::cross(n, t), bitangents[i]) < 0.0f ? -1.0f : 1.0f;
    vertex.tangent = glm::vec4{t, sign};
  }
}

} // namespace sonnet::renderer
