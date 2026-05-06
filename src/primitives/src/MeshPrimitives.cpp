#include <sonnet/primitives/MeshPrimitives.hpp>

#include <cmath>
#include <numbers>

namespace sonnet::primitives {

using sonnet::renderer::CPUMesh;
using sonnet::renderer::Vertex;

static constexpr float kPi = std::numbers::pi_v<float>;

// ── Box ───────────────────────────────────────────────────────────────────────

CPUMesh makeBox(glm::vec3 size) {
    const glm::vec3 h = size * 0.5F;

    // 6 faces, each with 4 verts and 1 flat normal.
    struct FaceSpec {
        glm::vec3 positions[4];
        glm::vec2 uvs[4];
        glm::vec3 normal;
    };

    const FaceSpec specs[6] = {
        // +X
        {{{+h.x, -h.y, -h.z}, {+h.x, +h.y, -h.z}, {+h.x, +h.y, +h.z}, {+h.x, -h.y, +h.z}},
         {{0.0F, 0.0F}, {0.0F, 1.0F}, {1.0F, 1.0F}, {1.0F, 0.0F}},
         {+1.0F, 0.0F, 0.0F}},
        // -X
        {{{-h.x, -h.y, +h.z}, {-h.x, +h.y, +h.z}, {-h.x, +h.y, -h.z}, {-h.x, -h.y, -h.z}},
         {{0.0F, 0.0F}, {0.0F, 1.0F}, {1.0F, 1.0F}, {1.0F, 0.0F}},
         {-1.0F, 0.0F, 0.0F}},
        // +Y
        {{{-h.x, +h.y, -h.z}, {-h.x, +h.y, +h.z}, {+h.x, +h.y, +h.z}, {+h.x, +h.y, -h.z}},
         {{0.0F, 0.0F}, {0.0F, 1.0F}, {1.0F, 1.0F}, {1.0F, 0.0F}},
         {0.0F, +1.0F, 0.0F}},
        // -Y
        {{{-h.x, -h.y, +h.z}, {-h.x, -h.y, -h.z}, {+h.x, -h.y, -h.z}, {+h.x, -h.y, +h.z}},
         {{0.0F, 0.0F}, {0.0F, 1.0F}, {1.0F, 1.0F}, {1.0F, 0.0F}},
         {0.0F, -1.0F, 0.0F}},
        // +Z
        {{{-h.x, -h.y, +h.z}, {+h.x, -h.y, +h.z}, {+h.x, +h.y, +h.z}, {-h.x, +h.y, +h.z}},
         {{0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F}},
         {0.0F, 0.0F, +1.0F}},
        // -Z
        {{{+h.x, -h.y, -h.z}, {-h.x, -h.y, -h.z}, {-h.x, +h.y, -h.z}, {+h.x, +h.y, -h.z}},
         {{0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F}},
         {0.0F, 0.0F, -1.0F}},
    };

    CPUMesh mesh;
    mesh.vertices.reserve(24);
    mesh.indices.reserve(36);

    for (uint32_t f = 0; f < 6; ++f) {
        const auto& s = specs[f];
        const uint32_t base = f * 4;
        for (int i = 0; i < 4; ++i) {
            mesh.vertices.push_back({s.positions[i], s.uvs[i], s.normal});
        }
        mesh.indices.insert(mesh.indices.end(),
                            {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    return mesh;
}

// ── UVSphere ─────────────────────────────────────────────────────────────────

CPUMesh makeUVSphere(int segX, int segY) {
    const auto sX = static_cast<uint32_t>(std::max(segX, 3));
    const auto sY = static_cast<uint32_t>(std::max(segY, 2));

    CPUMesh mesh;
    mesh.vertices.reserve((sX + 1) * (sY + 1));
    mesh.indices.reserve(sX * sY * 6);

    for (uint32_t y = 0; y <= sY; ++y) {
        const float v   = static_cast<float>(y) / static_cast<float>(sY);
        const float phi = v * kPi;
        for (uint32_t x = 0; x <= sX; ++x) {
            const float u     = static_cast<float>(x) / static_cast<float>(sX);
            const float theta = u * 2.0F * kPi;
            const glm::vec3 n{std::cos(theta) * std::sin(phi),
                               std::cos(phi),
                               std::sin(theta) * std::sin(phi)};
            mesh.vertices.push_back({n, {u, 1.0F - v}, n});
        }
    }

    const uint32_t stride = sX + 1;
    for (uint32_t y = 0; y < sY; ++y) {
        for (uint32_t x = 0; x < sX; ++x) {
            const uint32_t i0 = y * stride + x;
            const uint32_t i1 = i0 + 1;
            const uint32_t i2 = i0 + stride;
            const uint32_t i3 = i2 + 1;
            mesh.indices.insert(mesh.indices.end(), {i0, i1, i2, i1, i3, i2});
        }
    }
    return mesh;
}

// ── Cylinder ──────────────────────────────────────────────────────────────────

CPUMesh makeCylinder(float radius, float height, int segments) {
    const auto    seg   = static_cast<uint32_t>(std::max(segments, 3));
    const float   halfH = height * 0.5F;

    CPUMesh mesh;

    // Side: smooth radial normals.
    for (uint32_t i = 0; i <= seg; ++i) {
        const float t  = static_cast<float>(i) / static_cast<float>(seg);
        const float a  = t * 2.0F * kPi;
        const float cx = std::cos(a);
        const float cz = std::sin(a);
        const glm::vec3 n{cx, 0.0F, cz};
        mesh.vertices.push_back({{cx * radius, +halfH, cz * radius}, {t, 0.0F}, n});
        mesh.vertices.push_back({{cx * radius, -halfH, cz * radius}, {t, 1.0F}, n});
    }
    for (uint32_t i = 0; i < seg; ++i) {
        const uint32_t b = i * 2;
        mesh.indices.insert(mesh.indices.end(), {b, b + 2, b + 1, b + 1, b + 2, b + 3});
    }

    // Top cap: flat normal +Y.
    const uint32_t topCenterIdx = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({{0.0F, +halfH, 0.0F}, {0.5F, 0.5F}, {0.0F, 1.0F, 0.0F}});
    const uint32_t topRimBase = topCenterIdx + 1;
    for (uint32_t i = 0; i <= seg; ++i) {
        const float t  = static_cast<float>(i) / static_cast<float>(seg);
        const float a  = t * 2.0F * kPi;
        const float cx = std::cos(a);
        const float cz = std::sin(a);
        mesh.vertices.push_back(
            {{cx * radius, +halfH, cz * radius},
             {(cx + 1.0F) * 0.5F, (cz + 1.0F) * 0.5F},
             {0.0F, 1.0F, 0.0F}});
    }
    for (uint32_t i = 0; i < seg; ++i) {
        mesh.indices.insert(mesh.indices.end(),
                            {topCenterIdx, topRimBase + i, topRimBase + i + 1});
    }

    // Bottom cap: flat normal -Y, wound in reverse to face outward.
    const uint32_t botCenterIdx = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({{0.0F, -halfH, 0.0F}, {0.5F, 0.5F}, {0.0F, -1.0F, 0.0F}});
    const uint32_t botRimBase = botCenterIdx + 1;
    for (uint32_t i = 0; i <= seg; ++i) {
        const float t  = static_cast<float>(i) / static_cast<float>(seg);
        const float a  = t * 2.0F * kPi;
        const float cx = std::cos(a);
        const float cz = std::sin(a);
        mesh.vertices.push_back(
            {{cx * radius, -halfH, cz * radius},
             {(cx + 1.0F) * 0.5F, (cz + 1.0F) * 0.5F},
             {0.0F, -1.0F, 0.0F}});
    }
    for (uint32_t i = 0; i < seg; ++i) {
        mesh.indices.insert(mesh.indices.end(),
                            {botCenterIdx, botRimBase + i + 1, botRimBase + i});
    }

    return mesh;
}

// ── Plane ─────────────────────────────────────────────────────────────────────

CPUMesh makePlane(glm::vec2 size) {
    const glm::vec2 h    = size * 0.5F;
    const glm::vec3 norm = {0.0F, 1.0F, 0.0F};

    CPUMesh mesh;
    mesh.vertices = {
        {{-h.x, 0.0F, -h.y}, {0.0F, 0.0F}, norm},
        {{+h.x, 0.0F, -h.y}, {1.0F, 0.0F}, norm},
        {{+h.x, 0.0F, +h.y}, {1.0F, 1.0F}, norm},
        {{-h.x, 0.0F, +h.y}, {0.0F, 1.0F}, norm},
    };
    mesh.indices = {0, 1, 2, 0, 2, 3};
    return mesh;
}

// ── Capsule ───────────────────────────────────────────────────────────────────

CPUMesh makeCapsule(float radius, float height, int segments) {
    const auto    seg   = static_cast<uint32_t>(std::max(segments, 4));
    const float   halfH = height * 0.5F;
    const uint32_t hemRows = seg / 2;
    const uint32_t stride  = seg + 1;

    CPUMesh mesh;

    // Top hemisphere: phi from 0 to pi/2.
    for (uint32_t y = 0; y <= hemRows; ++y) {
        const float v   = static_cast<float>(y) / static_cast<float>(hemRows);
        const float phi = v * (kPi * 0.5F);
        for (uint32_t x = 0; x <= seg; ++x) {
            const float u     = static_cast<float>(x) / static_cast<float>(seg);
            const float theta = u * 2.0F * kPi;
            const glm::vec3 n{std::cos(theta) * std::sin(phi),
                               std::cos(phi),
                               std::sin(theta) * std::sin(phi)};
            mesh.vertices.push_back({n * radius + glm::vec3{0.0F, halfH, 0.0F}, {u, v * 0.5F}, n});
        }
    }
    for (uint32_t y = 0; y < hemRows; ++y) {
        for (uint32_t x = 0; x < seg; ++x) {
            const uint32_t i0 = y * stride + x;
            const uint32_t i1 = i0 + 1;
            const uint32_t i2 = i0 + stride;
            const uint32_t i3 = i2 + 1;
            mesh.indices.insert(mesh.indices.end(), {i0, i1, i2, i1, i3, i2});
        }
    }

    // Cylinder body: top ring and bottom ring.
    const uint32_t bodyBase = static_cast<uint32_t>(mesh.vertices.size());
    for (uint32_t x = 0; x <= seg; ++x) {
        const float u     = static_cast<float>(x) / static_cast<float>(seg);
        const float theta = u * 2.0F * kPi;
        const float cx    = std::cos(theta);
        const float cz    = std::sin(theta);
        const glm::vec3 n{cx, 0.0F, cz};
        mesh.vertices.push_back({{cx * radius, +halfH, cz * radius}, {u, 0.5F}, n});
        mesh.vertices.push_back({{cx * radius, -halfH, cz * radius}, {u, 0.5F}, n});
    }
    for (uint32_t x = 0; x < seg; ++x) {
        const uint32_t b = bodyBase + x * 2;
        mesh.indices.insert(mesh.indices.end(), {b, b + 2, b + 1, b + 1, b + 2, b + 3});
    }

    // Bottom hemisphere: phi from pi/2 to pi.
    const uint32_t botBase = static_cast<uint32_t>(mesh.vertices.size());
    for (uint32_t y = 0; y <= hemRows; ++y) {
        const float v   = static_cast<float>(y) / static_cast<float>(hemRows);
        const float phi = kPi * 0.5F + v * (kPi * 0.5F);
        for (uint32_t x = 0; x <= seg; ++x) {
            const float u     = static_cast<float>(x) / static_cast<float>(seg);
            const float theta = u * 2.0F * kPi;
            const glm::vec3 n{std::cos(theta) * std::sin(phi),
                               std::cos(phi),
                               std::sin(theta) * std::sin(phi)};
            mesh.vertices.push_back(
                {n * radius + glm::vec3{0.0F, -halfH, 0.0F}, {u, 0.5F + v * 0.5F}, n});
        }
    }
    for (uint32_t y = 0; y < hemRows; ++y) {
        for (uint32_t x = 0; x < seg; ++x) {
            const uint32_t i0 = botBase + y * stride + x;
            const uint32_t i1 = i0 + 1;
            const uint32_t i2 = i0 + stride;
            const uint32_t i3 = i2 + 1;
            mesh.indices.insert(mesh.indices.end(), {i0, i1, i2, i1, i3, i2});
        }
    }

    return mesh;
}

} // namespace sonnet::primitives
