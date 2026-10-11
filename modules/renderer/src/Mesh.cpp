#include <sonnet/renderer/Mesh.h>

#include <meshoptimizer.h>

#include <algorithm>
#include <cmath>
#include <limits>

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

void buildShadowRange(MeshData &mesh) {
  mesh.shadowIndices.clear();
  for (Submesh &submesh : mesh.submeshes) {
    submesh.shadowFirstIndex = 0;
    submesh.shadowIndexCount = 0;
    submesh.shadowError = 0.0f;
  }
  if (!mesh.skin.empty() || mesh.morphTargetCount > 0 || mesh.vertices.empty()) {
    return; // a deformed draw reads its own vertex buffer, which the coarse range is not made for
  }
  const Bounds bounds = mesh.bounds();
  const glm::vec3 size = bounds.max - bounds.min;
  const float radius = 0.5f * std::max({size.x, size.y, size.z});
  if (radius <= 0.0f) {
    return;
  }
  if (mesh.submeshes.empty()) {
    mesh.submeshes.push_back(Submesh{0, static_cast<std::uint32_t>(mesh.indices.size()), 0});
  }
  // The simplifier needs the topology of the surface, which a mesh with a vertex per corner or per
  // seam does not have: weld the positions for it, then name each result vertex by the first
  // original vertex at that position.
  std::vector<std::uint32_t> remap(mesh.vertices.size());
  const meshopt_Stream stream{&mesh.vertices[0].position, sizeof(glm::vec3), sizeof(Vertex)};
  const std::size_t uniqueCount =
      meshopt_generateVertexRemapMulti(remap.data(), nullptr, mesh.vertices.size(), mesh.vertices.size(), &stream, 1);
  std::vector<glm::vec3> positions(uniqueCount);
  std::vector<std::uint32_t> representative(uniqueCount, std::numeric_limits<std::uint32_t>::max());
  for (std::uint32_t i = 0; i < mesh.vertices.size(); ++i) {
    if (representative[remap[i]] == std::numeric_limits<std::uint32_t>::max()) {
      representative[remap[i]] = i;
      positions[remap[i]] = mesh.vertices[i].position;
    }
  }
  std::vector<std::uint32_t> welded;
  std::vector<std::uint32_t> simplified;
  for (Submesh &submesh : mesh.submeshes) {
    if (submesh.indexCount % 3 != 0 || submesh.indexCount / 3 < ShadowRangeMinTriangles ||
        std::size_t{submesh.firstIndex} + submesh.indexCount > mesh.indices.size()) {
      continue;
    }
    welded.resize(submesh.indexCount);
    for (std::uint32_t i = 0; i < submesh.indexCount; ++i) {
      welded[i] = remap[mesh.indices[submesh.firstIndex + i]];
    }
    simplified.resize(submesh.indexCount);
    float error = 0.0f;
    const std::size_t count =
        meshopt_simplify(simplified.data(), welded.data(), submesh.indexCount, &positions[0].x, uniqueCount,
                         sizeof(glm::vec3), 0, ShadowRangeError * radius / ShadowRangeErrorMargin,
                         meshopt_SimplifyLockBorder | meshopt_SimplifyErrorAbsolute, &error);
    if (count == 0 || count * 2 > submesh.indexCount) {
      continue;
    }
    submesh.shadowFirstIndex = static_cast<std::uint32_t>(mesh.indices.size() + mesh.shadowIndices.size());
    submesh.shadowIndexCount = static_cast<std::uint32_t>(count);
    submesh.shadowError = error * ShadowRangeErrorMargin / radius;
    for (std::size_t i = 0; i < count; ++i) {
      mesh.shadowIndices.push_back(representative[simplified[i]]);
    }
  }
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
