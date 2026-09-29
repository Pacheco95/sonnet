#include <sonnet/editor/LightLines.h>

#include <glm/glm.hpp>

#include <cmath>
#include <numbers>

namespace sonnet::editor {

namespace {

constexpr int RingSegments = 32;
constexpr int SideLines = 4;
constexpr float InnerDimming = 0.4f;
// A spot light's cone is undefined at and past a right angle; the renderer clamps it the same way.
constexpr float MaxAngle = 1.55f;
constexpr float Tau = 2.0f * std::numbers::pi_v<float>;

void ring(glm::vec3 centre, glm::vec3 u, glm::vec3 v, float radius, glm::vec4 color,
          std::vector<renderer::DebugLine> &lines) {
  glm::vec3 previous = centre + radius * u;
  for (int i = 1; i <= RingSegments; ++i) {
    const float angle = Tau * static_cast<float>(i) / static_cast<float>(RingSegments);
    const glm::vec3 next = centre + radius * (std::cos(angle) * u + std::sin(angle) * v);
    lines.push_back({.from = previous, .to = next, .color = color});
    previous = next;
  }
}

} // namespace

void appendSpotLightLines(std::span<const renderer::Light> lights, std::vector<renderer::DebugLine> &lines) {
  for (const renderer::Light &light : lights) {
    if (light.type != renderer::LightType::Spot) {
      continue;
    }
    const glm::vec3 axis = glm::normalize(light.direction);
    // Any unit vector across the axis will do as the ring's first axis.
    const glm::vec3 helper = std::abs(axis.y) < 0.99f ? glm::vec3{0.0f, 1.0f, 0.0f} : glm::vec3{1.0f, 0.0f, 0.0f};
    const glm::vec3 u = glm::normalize(glm::cross(axis, helper));
    const glm::vec3 v = glm::cross(axis, u);
    const glm::vec3 base = light.position + axis * light.range;
    const float outer = light.range * std::tan(glm::clamp(light.outerAngle, 0.0f, MaxAngle));
    const float inner = light.range * std::tan(glm::clamp(light.innerAngle, 0.0f, MaxAngle));
    const glm::vec4 color{light.color, 1.0f};

    ring(base, u, v, outer, color, lines);
    ring(base, u, v, inner, glm::vec4{light.color * InnerDimming, 1.0f}, lines);
    for (int i = 0; i < SideLines; ++i) {
      const float angle = Tau * static_cast<float>(i) / static_cast<float>(SideLines);
      lines.push_back({.from = light.position,
                       .to = base + outer * (std::cos(angle) * u + std::sin(angle) * v),
                       .color = color});
    }
  }
}

} // namespace sonnet::editor
