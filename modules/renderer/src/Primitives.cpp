#include <sonnet/renderer/Primitives.h>

#include <sonnet/core/Assert.h>

#include <cmath>
#include <numbers>

namespace sonnet::renderer::primitives {

namespace {

constexpr float Pi = std::numbers::pi_v<float>;

void addQuad(MeshData &mesh, glm::vec3 origin, glm::vec3 right, glm::vec3 up, glm::vec3 normal) {
  const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
  mesh.vertices.push_back({.position = origin, .normal = normal, .uv = {0.0f, 0.0f}});
  mesh.vertices.push_back({.position = origin + right, .normal = normal, .uv = {1.0f, 0.0f}});
  mesh.vertices.push_back({.position = origin + right + up, .normal = normal, .uv = {1.0f, 1.0f}});
  mesh.vertices.push_back({.position = origin + up, .normal = normal, .uv = {0.0f, 1.0f}});
  // Counter-clockwise seen from the direction the normal points to.
  mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}

// A band of stacked rings between two rows of vertices, `slices + 1` vertices per row with the
// seam vertex duplicated for texture coordinates. A row at a pole has all its vertices in one
// place, so the band ends in one triangle per slice instead of two degenerate ones.
void addBand(MeshData &mesh, std::uint32_t firstRow, std::uint32_t secondRow, std::uint32_t slices,
             bool firstIsPole = false, bool secondIsPole = false) {
  for (std::uint32_t i = 0; i < slices; ++i) {
    const std::uint32_t a = firstRow + i;
    const std::uint32_t b = firstRow + i + 1;
    const std::uint32_t c = secondRow + i;
    const std::uint32_t d = secondRow + i + 1;
    if (!secondIsPole) {
      mesh.indices.insert(mesh.indices.end(), {a, c, d});
    }
    if (!firstIsPole) {
      mesh.indices.insert(mesh.indices.end(), {a, d, b});
    }
  }
}

// A row of vertices around the Y axis at height y with the given radius; the normal is the
// direction from `normalOrigin` so hemispheres and cylinders share it.
std::uint32_t addRing(MeshData &mesh, float y, float radius, std::uint32_t slices, glm::vec3 normalOrigin, float v) {
  const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
  for (std::uint32_t i = 0; i <= slices; ++i) {
    const float u = static_cast<float>(i) / static_cast<float>(slices);
    const float angle = u * 2.0f * Pi;
    // Counter-clockwise seen from +Y when walking with increasing angle from +X towards -Z.
    const glm::vec3 position{radius * std::cos(angle), y, -radius * std::sin(angle)};
    const glm::vec3 offset = position - normalOrigin;
    const glm::vec3 normal = glm::length(offset) > 0.0f ? glm::normalize(offset) : glm::vec3{0.0f, 1.0f, 0.0f};
    mesh.vertices.push_back({.position = position, .normal = normal, .uv = {u, v}});
  }
  return first;
}

// A flat disc at height y facing `normal`, as a fan around a centre vertex.
void addCap(MeshData &mesh, float y, float radius, std::uint32_t slices, glm::vec3 normal) {
  const auto centre = static_cast<std::uint32_t>(mesh.vertices.size());
  mesh.vertices.push_back({.position = {0.0f, y, 0.0f}, .normal = normal, .uv = {0.5f, 0.5f}});
  for (std::uint32_t i = 0; i <= slices; ++i) {
    const float angle = static_cast<float>(i) / static_cast<float>(slices) * 2.0f * Pi;
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    mesh.vertices.push_back(
        {.position = {radius * c, y, -radius * s}, .normal = normal, .uv = {0.5f + 0.5f * c, 0.5f + 0.5f * s}});
  }
  for (std::uint32_t i = 0; i < slices; ++i) {
    const std::uint32_t a = centre + 1 + i;
    const std::uint32_t b = centre + 2 + i;
    if (normal.y > 0.0f) {
      mesh.indices.insert(mesh.indices.end(), {centre, a, b});
    } else {
      mesh.indices.insert(mesh.indices.end(), {centre, b, a});
    }
  }
}

// Rings of a hemisphere from the equator (ringIndex 0) to the pole, excluding the pole itself.
void addHemisphereRings(MeshData &mesh, float centreY, float radius, std::uint32_t slices, std::uint32_t rings,
                        bool upper, float vStart, float vEnd) {
  const glm::vec3 centre{0.0f, centreY, 0.0f};
  for (std::uint32_t r = 0; r <= rings; ++r) {
    const float t = static_cast<float>(r) / static_cast<float>(rings); // 0 equator, 1 pole
    const float latitude = t * Pi * 0.5f;
    const float ringRadius = radius * std::cos(latitude);
    const float y = centreY + (upper ? 1.0f : -1.0f) * radius * std::sin(latitude);
    static_cast<void>(addRing(mesh, y, ringRadius, slices, centre, vStart + (vEnd - vStart) * t));
  }
}

} // namespace

MeshData box(glm::vec3 h) {
  MeshData mesh;
  mesh.vertices.reserve(24);
  mesh.indices.reserve(36);
  addQuad(mesh, {-h.x, -h.y, h.z}, {2 * h.x, 0, 0}, {0, 2 * h.y, 0}, {0, 0, 1});   // +Z
  addQuad(mesh, {h.x, -h.y, -h.z}, {-2 * h.x, 0, 0}, {0, 2 * h.y, 0}, {0, 0, -1}); // -Z
  addQuad(mesh, {h.x, -h.y, h.z}, {0, 0, -2 * h.z}, {0, 2 * h.y, 0}, {1, 0, 0});   // +X
  addQuad(mesh, {-h.x, -h.y, -h.z}, {0, 0, 2 * h.z}, {0, 2 * h.y, 0}, {-1, 0, 0}); // -X
  addQuad(mesh, {-h.x, h.y, h.z}, {2 * h.x, 0, 0}, {0, 0, -2 * h.z}, {0, 1, 0});   // +Y
  addQuad(mesh, {-h.x, -h.y, -h.z}, {2 * h.x, 0, 0}, {0, 0, 2 * h.z}, {0, -1, 0}); // -Y
  generateTangents(mesh);
  return mesh;
}

MeshData sphere(float radius, std::uint32_t slices, std::uint32_t stacks) {
  SONNET_ASSERT(slices >= 3 && stacks >= 2, "sphere needs at least 3 slices and 2 stacks");
  MeshData mesh;
  for (std::uint32_t s = 0; s <= stacks; ++s) {
    const float v = static_cast<float>(s) / static_cast<float>(stacks);
    const float latitude = (0.5f - v) * Pi; // +pi/2 at the top
    const float y = radius * std::sin(latitude);
    const float ringRadius = radius * std::cos(latitude);
    static_cast<void>(addRing(mesh, y, ringRadius, slices, {0.0f, 0.0f, 0.0f}, v));
  }
  for (std::uint32_t s = 0; s < stacks; ++s) {
    addBand(mesh, s * (slices + 1), (s + 1) * (slices + 1), slices, s == 0, s + 1 == stacks);
  }
  generateTangents(mesh);
  return mesh;
}

MeshData plane(glm::vec2 size) {
  MeshData mesh;
  addQuad(mesh, {-size.x * 0.5f, 0.0f, size.y * 0.5f}, {size.x, 0.0f, 0.0f}, {0.0f, 0.0f, -size.y}, {0.0f, 1.0f, 0.0f});
  generateTangents(mesh);
  return mesh;
}

MeshData cylinder(float radius, float height, std::uint32_t slices) {
  SONNET_ASSERT(slices >= 3, "cylinder needs at least 3 slices");
  MeshData mesh;
  const float h = height * 0.5f;
  // Side normals point away from the axis: the normal origin is the ring's own centre.
  const std::uint32_t bottom = addRing(mesh, -h, radius, slices, {0.0f, -h, 0.0f}, 0.0f);
  const std::uint32_t top = addRing(mesh, h, radius, slices, {0.0f, h, 0.0f}, 1.0f);
  addBand(mesh, top, bottom, slices);
  addCap(mesh, h, radius, slices, {0.0f, 1.0f, 0.0f});
  addCap(mesh, -h, radius, slices, {0.0f, -1.0f, 0.0f});
  generateTangents(mesh);
  return mesh;
}

MeshData capsule(float radius, float height, std::uint32_t slices, std::uint32_t rings) {
  SONNET_ASSERT(slices >= 3 && rings >= 1, "capsule needs at least 3 slices and 1 ring");
  SONNET_ASSERT(height >= 2.0f * radius, "capsule height {} is shorter than two radii", height);
  MeshData mesh;
  const float half = height * 0.5f - radius; // half length of the straight part
  // Upper hemisphere from the equator up, straight part, lower hemisphere from the equator down.
  const std::uint32_t upperFirst = static_cast<std::uint32_t>(mesh.vertices.size());
  addHemisphereRings(mesh, half, radius, slices, rings, true, 0.5f, 0.0f);
  for (std::uint32_t r = 0; r < rings; ++r) {
    // Ring r is nearer the equator than ring r + 1; the band's first row is the upper one.
    addBand(mesh, upperFirst + (r + 1) * (slices + 1), upperFirst + r * (slices + 1), slices, r + 1 == rings, false);
  }
  const std::uint32_t lowerFirst = static_cast<std::uint32_t>(mesh.vertices.size());
  addHemisphereRings(mesh, -half, radius, slices, rings, false, 0.5f, 1.0f);
  for (std::uint32_t r = 0; r < rings; ++r) {
    addBand(mesh, lowerFirst + r * (slices + 1), lowerFirst + (r + 1) * (slices + 1), slices, false, r + 1 == rings);
  }
  addBand(mesh, upperFirst, lowerFirst, slices);
  generateTangents(mesh);
  return mesh;
}

} // namespace sonnet::renderer::primitives
