#include <sonnet/renderer/Primitives.h>

#include <sonnet/core/Assert.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <unordered_map>

namespace sonnet::renderer::primitives {

namespace {

constexpr float Pi = std::numbers::pi_v<float>;

// The texture coordinates run 0 to 1 over the quad, or with `metric` over its edge lengths in metres, so
// faces of different sizes share one texel density and the texture repeats instead of stretching.
void addQuad(MeshData &mesh, glm::vec3 origin, glm::vec3 right, glm::vec3 up, glm::vec3 normal, bool metric = false) {
  const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
  const float u = metric ? glm::length(right) : 1.0f;
  const float v = metric ? glm::length(up) : 1.0f;
  mesh.vertices.push_back({.position = origin, .normal = normal, .uv = {0.0f, 0.0f}});
  mesh.vertices.push_back({.position = origin + right, .normal = normal, .uv = {u, 0.0f}});
  mesh.vertices.push_back({.position = origin + right + up, .normal = normal, .uv = {u, v}});
  mesh.vertices.push_back({.position = origin + up, .normal = normal, .uv = {0.0f, v}});
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

// A quad from four corners, counter-clockwise seen from where the normals point, each with its own
// normal and texture coordinate. For faces that are not parallelograms or that are smooth-shaded.
void addShadedQuad(MeshData &mesh, const std::array<glm::vec3, 4> &p, const std::array<glm::vec3, 4> &n,
                   const std::array<glm::vec2, 4> &uv) {
  const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
  for (std::size_t i = 0; i < 4; ++i) {
    mesh.vertices.push_back({.position = p[i], .normal = n[i], .uv = uv[i]});
  }
  mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}

void addTriangle(MeshData &mesh, const std::array<glm::vec3, 3> &p, const std::array<glm::vec2, 3> &uv,
                 glm::vec3 normal) {
  const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
  for (std::size_t i = 0; i < 3; ++i) {
    mesh.vertices.push_back({.position = p[i], .normal = normal, .uv = uv[i]});
  }
  mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2});
}

// One point of a surface of revolution about the Y axis; `normal` is (radial, y) and is rotated with the angle.
struct ProfilePoint {
  float radius;
  float y;
  glm::vec2 normal;
  float v;
};

// Sweeps a profile (top to bottom) around the Y axis. A point on the axis is a pole: the band next to it
// has one triangle per slice.
void addRevolved(MeshData &mesh, const std::vector<ProfilePoint> &profile, std::uint32_t slices) {
  const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
  for (const ProfilePoint &point : profile) {
    for (std::uint32_t i = 0; i <= slices; ++i) {
      const float u = static_cast<float>(i) / static_cast<float>(slices);
      const float angle = u * 2.0f * Pi;
      const float c = std::cos(angle);
      const float s = std::sin(angle);
      mesh.vertices.push_back({.position = {point.radius * c, point.y, -point.radius * s},
                               .normal = {point.normal.x * c, point.normal.y, -point.normal.x * s},
                               .uv = {u, point.v}});
    }
  }
  for (std::size_t r = 0; r + 1 < profile.size(); ++r) {
    addBand(mesh, first + static_cast<std::uint32_t>(r) * (slices + 1),
            first + static_cast<std::uint32_t>(r + 1) * (slices + 1), slices, profile[r].radius == 0.0f,
            profile[r + 1].radius == 0.0f);
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

MeshData cone(float radius, float height, std::uint32_t slices) {
  SONNET_ASSERT(slices >= 3, "cone needs at least 3 slices");
  MeshData mesh;
  const float h = height * 0.5f;
  // The side normal leans towards +Y by the slope: perpendicular to the line from the base rim to the apex.
  const glm::vec2 side = glm::normalize(glm::vec2{height, radius});
  addRevolved(mesh, {{0.0f, h, side, 0.0f}, {radius, -h, side, 1.0f}}, slices);
  addCap(mesh, -h, radius, slices, {0.0f, -1.0f, 0.0f});
  generateTangents(mesh);
  return mesh;
}

MeshData torus(float majorRadius, float minorRadius, std::uint32_t majorSegments, std::uint32_t minorSegments) {
  SONNET_ASSERT(majorSegments >= 3 && minorSegments >= 3, "torus needs at least 3 segments both ways");
  MeshData mesh;
  for (std::uint32_t i = 0; i <= majorSegments; ++i) {
    const float u = static_cast<float>(i) / static_cast<float>(majorSegments);
    const float angle = u * 2.0f * Pi;
    const glm::vec3 outward{std::cos(angle), 0.0f, -std::sin(angle)};
    for (std::uint32_t j = 0; j <= minorSegments; ++j) {
      const float v = static_cast<float>(j) / static_cast<float>(minorSegments);
      const float tube = v * 2.0f * Pi;
      const glm::vec3 normal = outward * std::cos(tube) + glm::vec3{0.0f, std::sin(tube), 0.0f};
      mesh.vertices.push_back(
          {.position = outward * majorRadius + normal * minorRadius, .normal = normal, .uv = {u, v}});
    }
  }
  const std::uint32_t stride = minorSegments + 1;
  for (std::uint32_t i = 0; i < majorSegments; ++i) {
    for (std::uint32_t j = 0; j < minorSegments; ++j) {
      const std::uint32_t a = i * stride + j;
      const std::uint32_t b = a + stride;
      mesh.indices.insert(mesh.indices.end(), {a, b, b + 1, a, b + 1, a + 1});
    }
  }
  generateTangents(mesh);
  return mesh;
}

MeshData ramp(glm::vec3 size) {
  MeshData mesh;
  const glm::vec3 h = size * 0.5f;
  addQuad(mesh, {h.x, -h.y, -h.z}, {-2 * h.x, 0, 0}, {0, 2 * h.y, 0}, {0, 0, -1}, true); // back wall
  addQuad(mesh, {-h.x, -h.y, -h.z}, {2 * h.x, 0, 0}, {0, 0, 2 * h.z}, {0, -1, 0}, true); // bottom
  addQuad(mesh, {-h.x, -h.y, h.z}, {2 * h.x, 0, 0}, {0, 2 * h.y, -2 * h.z}, glm::normalize(glm::vec3{0, h.z, h.y}),
          true); // slope
  addTriangle(mesh, {{{h.x, -h.y, h.z}, {h.x, -h.y, -h.z}, {h.x, h.y, -h.z}}},
              {{{0, 0}, {size.z, 0}, {size.z, size.y}}}, {1, 0, 0});
  addTriangle(mesh, {{{-h.x, -h.y, h.z}, {-h.x, h.y, -h.z}, {-h.x, -h.y, -h.z}}},
              {{{0, 0}, {size.z, size.y}, {size.z, 0}}}, {-1, 0, 0});
  generateTangents(mesh);
  return mesh;
}

MeshData stairs(glm::vec3 size, std::uint32_t steps) {
  SONNET_ASSERT(steps >= 1, "stairs need at least one step");
  MeshData mesh;
  const glm::vec3 h = size * 0.5f;
  const float rise = size.y / static_cast<float>(steps);
  const float run = size.z / static_cast<float>(steps);
  addQuad(mesh, {h.x, -h.y, -h.z}, {-2 * h.x, 0, 0}, {0, 2 * h.y, 0}, {0, 0, -1}, true); // back wall
  addQuad(mesh, {-h.x, -h.y, -h.z}, {2 * h.x, 0, 0}, {0, 0, 2 * h.z}, {0, -1, 0}, true); // bottom
  for (std::uint32_t i = 0; i < steps; ++i) {
    const float front = h.z - static_cast<float>(i) * run;
    const float back = front - run;
    const float low = -h.y + static_cast<float>(i) * rise;
    const float top = low + rise;
    addQuad(mesh, {-h.x, low, front}, {2 * h.x, 0, 0}, {0, rise, 0}, {0, 0, 1}, true);   // riser
    addQuad(mesh, {-h.x, top, front}, {2 * h.x, 0, 0}, {0, 0, -run}, {0, 1, 0}, true);   // tread
    addQuad(mesh, {h.x, -h.y, front}, {0, 0, -run}, {0, top + h.y, 0}, {1, 0, 0}, true); // right side
    addQuad(mesh, {-h.x, -h.y, back}, {0, 0, run}, {0, top + h.y, 0}, {-1, 0, 0}, true); // left side
  }
  generateTangents(mesh);
  return mesh;
}

MeshData hemisphere(float radius, std::uint32_t slices, std::uint32_t rings) {
  SONNET_ASSERT(slices >= 3 && rings >= 1, "hemisphere needs at least 3 slices and 1 ring");
  MeshData mesh;
  const float baseY = -radius * 0.5f; // the bounds are centred on the origin
  std::vector<ProfilePoint> profile;
  for (std::uint32_t r = 0; r <= rings; ++r) {
    const float t = static_cast<float>(r) / static_cast<float>(rings); // 0 at the pole, 1 at the equator
    const float latitude = (1.0f - t) * Pi * 0.5f;
    // The pole is exactly on the axis, which addRevolved needs to see, so it is not left to cos(pi / 2).
    const float cosine = r == 0 ? 0.0f : std::cos(latitude);
    profile.push_back({radius * cosine, baseY + radius * std::sin(latitude), {cosine, std::sin(latitude)}, t});
  }
  addRevolved(mesh, profile, slices);
  // Texture the dome from its pole, in metres: the distance along the surface from the pole is the radius of
  // a disc in the texture. A latitude/longitude mapping would pinch the whole texture into the top.
  for (Vertex &vertex : mesh.vertices) {
    const float distance = radius * std::acos(std::clamp(vertex.normal.y, -1.0f, 1.0f));
    const float angle = std::atan2(-vertex.position.z, vertex.position.x);
    vertex.uv = {distance * std::cos(angle), distance * std::sin(angle)};
  }
  addCap(mesh, baseY, radius, slices, {0.0f, -1.0f, 0.0f});
  generateTangents(mesh);
  return mesh;
}

MeshData arch(glm::vec3 size, std::uint32_t segments) {
  SONNET_ASSERT(segments >= 2, "arch needs at least 2 segments");
  SONNET_ASSERT(size.y >= size.x * 0.5f, "arch height {} is less than half its width {}", size.y, size.x);
  MeshData mesh;
  const glm::vec3 h = size * 0.5f;
  const float r = 0.6f * h.x;                // the opening's radius
  const float spring = h.y - 0.4f * h.x - r; // where the straight sides end and the round top starts
  // Metres from the front face's lower left corner, so every face of the arch has the same texel density.
  const auto planar = [&](glm::vec3 p) { return glm::vec2{p.x + h.x, p.y + h.y}; };

  // Front and back: a pier either side and, over the opening, one trapezoid per arc segment up to the top.
  addQuad(mesh, {-h.x, -h.y, h.z}, {h.x - r, 0, 0}, {0, size.y, 0}, {0, 0, 1}, true);
  addQuad(mesh, {r, -h.y, h.z}, {h.x - r, 0, 0}, {0, size.y, 0}, {0, 0, 1}, true);
  addQuad(mesh, {-r, -h.y, -h.z}, {-(h.x - r), 0, 0}, {0, size.y, 0}, {0, 0, -1}, true);
  addQuad(mesh, {h.x, -h.y, -h.z}, {-(h.x - r), 0, 0}, {0, size.y, 0}, {0, 0, -1}, true);
  std::vector<glm::vec2> arc; // from the left spring line over the crown to the right one
  for (std::uint32_t k = 0; k <= segments; ++k) {
    const float angle = Pi * (1.0f - static_cast<float>(k) / static_cast<float>(segments));
    arc.push_back({r * std::cos(angle), spring + r * std::sin(angle)});
  }
  std::vector<float> arcLength{0.0f};
  for (std::uint32_t k = 0; k < segments; ++k) {
    arcLength.push_back(arcLength.back() + glm::length(arc[k + 1] - arc[k]));
  }
  for (std::uint32_t k = 0; k < segments; ++k) {
    const glm::vec3 a{arc[k], h.z}, b{arc[k + 1], h.z};
    const glm::vec3 c{arc[k + 1].x, h.y, h.z}, d{arc[k].x, h.y, h.z};
    addShadedQuad(mesh, {a, b, c, d}, {glm::vec3{0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}},
                  {planar(a), planar(b), planar(c), planar(d)});
    const glm::vec3 a2{a.x, a.y, -h.z}, b2{b.x, b.y, -h.z}, c2{c.x, c.y, -h.z}, d2{d.x, d.y, -h.z};
    const auto mirrored = [&](glm::vec3 p) { return glm::vec2{size.x - planar(p).x, planar(p).y}; };
    addShadedQuad(mesh, {a2, d2, c2, b2}, {glm::vec3{0, 0, -1}, {0, 0, -1}, {0, 0, -1}, {0, 0, -1}},
                  {mirrored(a2), mirrored(d2), mirrored(c2), mirrored(b2)});
  }

  // The outside: both sides, the top and the feet of the piers.
  addQuad(mesh, {h.x, -h.y, h.z}, {0, 0, -2 * h.z}, {0, size.y, 0}, {1, 0, 0}, true);
  addQuad(mesh, {-h.x, -h.y, -h.z}, {0, 0, 2 * h.z}, {0, size.y, 0}, {-1, 0, 0}, true);
  addQuad(mesh, {-h.x, h.y, h.z}, {size.x, 0, 0}, {0, 0, -2 * h.z}, {0, 1, 0}, true);
  addQuad(mesh, {-h.x, -h.y, -h.z}, {h.x - r, 0, 0}, {0, 0, 2 * h.z}, {0, -1, 0}, true);
  addQuad(mesh, {r, -h.y, -h.z}, {h.x - r, 0, 0}, {0, 0, 2 * h.z}, {0, -1, 0}, true);

  // The tunnel: the straight sides up to the spring line, then the smooth curve overhead, all facing inward.
  const float wall = spring + h.y;
  if (wall > 1e-6f) {
    addQuad(mesh, {-r, -h.y, h.z}, {0, 0, -2 * h.z}, {0, wall, 0}, {1, 0, 0}, true);
    addQuad(mesh, {r, -h.y, -h.z}, {0, 0, 2 * h.z}, {0, wall, 0}, {-1, 0, 0}, true);
  }
  for (std::uint32_t k = 0; k < segments; ++k) {
    const glm::vec3 inA = -glm::normalize(glm::vec3{arc[k].x, arc[k].y - spring, 0.0f});
    const glm::vec3 inB = -glm::normalize(glm::vec3{arc[k + 1].x, arc[k + 1].y - spring, 0.0f});
    // Along the curve in metres, across the depth in metres.
    const float u0 = arcLength[k];
    const float u1 = arcLength[k + 1];
    addShadedQuad(mesh, {glm::vec3{arc[k], h.z}, {arc[k], -h.z}, {arc[k + 1], -h.z}, {arc[k + 1], h.z}},
                  {inA, inA, inB, inB}, {glm::vec2{u0, 0}, {u0, size.z}, {u1, size.z}, {u1, 0}});
  }
  generateTangents(mesh);
  return mesh;
}

MeshData icosphere(float radius, std::uint32_t subdivisions) {
  SONNET_ASSERT(subdivisions <= 7, "icosphere with {} subdivisions is too large", subdivisions);
  // An icosahedron with a vertex on each pole and two rings of five between them, so the texture's
  // poles coincide with real vertices.
  const float ringY = 1.0f / std::sqrt(5.0f);
  const float ringRadius = 2.0f * ringY;
  std::vector<glm::vec3> points{{0.0f, 1.0f, 0.0f}};
  for (std::uint32_t k = 0; k < 10; ++k) {
    const float angle =
        static_cast<float>(k) * Pi / 5.0f; // even k is the upper ring, odd k the lower, offset by 36 degrees
    points.push_back({ringRadius * std::cos(angle), k % 2 == 0 ? ringY : -ringY, -ringRadius * std::sin(angle)});
  }
  points.push_back({0.0f, -1.0f, 0.0f});
  const auto upper = [](std::uint32_t k) { return 1 + (k % 5) * 2; };
  const auto lower = [](std::uint32_t k) { return 2 + (k % 5) * 2; };
  std::vector<std::array<std::uint32_t, 3>> faces;
  for (std::uint32_t k = 0; k < 5; ++k) {
    faces.push_back({0, upper(k), upper(k + 1)});
    faces.push_back({upper(k), lower(k), upper(k + 1)});
    faces.push_back({upper(k + 1), lower(k), lower(k + 1)});
    faces.push_back({11, lower(k + 1), lower(k)});
  }
  // Wound counter-clockwise seen from outside: the centroid direction is the outward one.
  for (auto &face : faces) {
    const glm::vec3 &a = points[face[0]];
    const glm::vec3 &b = points[face[1]];
    const glm::vec3 &c = points[face[2]];
    if (glm::dot(glm::cross(b - a, c - a), a + b + c) < 0.0f) {
      std::swap(face[1], face[2]);
    }
  }
  for (std::uint32_t level = 0; level < subdivisions; ++level) {
    std::unordered_map<std::uint64_t, std::uint32_t> midpoints;
    const auto midpoint = [&](std::uint32_t a, std::uint32_t b) {
      const std::uint64_t key = (static_cast<std::uint64_t>(std::min(a, b)) << 32) | std::max(a, b);
      const auto [it, inserted] = midpoints.try_emplace(key, static_cast<std::uint32_t>(points.size()));
      if (inserted) {
        points.push_back(glm::normalize(points[a] + points[b]));
      }
      return it->second;
    };
    std::vector<std::array<std::uint32_t, 3>> next;
    next.reserve(faces.size() * 4);
    for (const auto &[a, b, c] : faces) {
      const std::uint32_t ab = midpoint(a, b);
      const std::uint32_t bc = midpoint(b, c);
      const std::uint32_t ca = midpoint(c, a);
      next.insert(next.end(), {{a, ab, ca}, {b, bc, ab}, {c, ca, bc}, {ab, bc, ca}});
    }
    faces = std::move(next);
  }

  // Equirectangular coordinates, per triangle so none of them stretches across the seam or the poles: the
  // longitudes of a triangle are unwrapped to within half a turn of its first off-axis corner (past 0 or 1
  // where they must be; the sampler repeats), and a corner on the axis takes the mean of the others'.
  const auto longitude = [](glm::vec3 p) { return std::atan2(-p.z, p.x) / (2.0f * Pi); };
  const auto onAxis = [](glm::vec3 p) { return p.x * p.x + p.z * p.z < 1e-8f; };
  MeshData mesh;
  mesh.vertices.reserve(faces.size() * 3);
  for (const auto &face : faces) {
    std::array<float, 3> u{};
    std::size_t reference = 0;
    while (reference < 3 && onAxis(points[face[reference]])) {
      ++reference;
    }
    const float base = longitude(points[face[reference]]);
    float sum = 0.0f;
    int count = 0;
    for (std::size_t i = 0; i < 3; ++i) {
      if (onAxis(points[face[i]])) {
        continue;
      }
      float value = longitude(points[face[i]]);
      value -= std::round(value - base); // within half a turn of the reference
      u[i] = value;
      sum += value;
      ++count;
    }
    for (std::size_t i = 0; i < 3; ++i) {
      if (onAxis(points[face[i]])) {
        u[i] = sum / static_cast<float>(count);
      }
    }
    // Keep the triangle's coordinates near [0, 1] rather than wherever atan2's branch put the reference.
    const float shift = -std::floor(*std::ranges::min_element(u) + 1e-4f);
    for (std::size_t i = 0; i < 3; ++i) {
      const glm::vec3 &p = points[face[i]];
      mesh.indices.push_back(static_cast<std::uint32_t>(mesh.vertices.size()));
      mesh.vertices.push_back({.position = p * radius, .normal = p, .uv = {u[i] + shift, 0.5f - std::asin(p.y) / Pi}});
    }
  }
  generateTangents(mesh);
  return mesh;
}

} // namespace sonnet::renderer::primitives
