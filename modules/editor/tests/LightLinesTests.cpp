#include <sonnet/editor/LightLines.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

using namespace sonnet;
using Catch::Approx;

TEST_CASE("a spot light's cone has its apex, axis and base radius", "[editor][lights]") {
  const renderer::Light light{.type = renderer::LightType::Spot,
                              .position = {1.0f, 2.0f, 3.0f},
                              .color = {1.0f, 0.5f, 0.25f},
                              .range = 4.0f,
                              .direction = {1.0f, 0.0f, 0.0f},
                              .innerAngle = glm::quarter_pi<float>() / 2.0f,
                              .outerAngle = glm::quarter_pi<float>()};
  std::vector<renderer::DebugLine> lines;
  editor::appendSpotLightLines(std::span{&light, 1}, lines);
  REQUIRE(!lines.empty());

  const glm::vec3 baseCentre = light.position + light.direction * light.range;
  int fromApex = 0;
  for (const renderer::DebugLine &line : lines) {
    if (line.from == light.position) {
      ++fromApex;
      // The side lines end on the outer ring: 4 units along the axis, radius 4 * tan(45 degrees).
      REQUIRE(glm::dot(line.to - light.position, light.direction) == Approx(4.0f));
      REQUIRE(glm::length(line.to - baseCentre) == Approx(4.0f));
      REQUIRE(line.color == glm::vec4{light.color, 1.0f});
    } else {
      // Every ring vertex sits in the base plane.
      REQUIRE(glm::dot(line.from - baseCentre, light.direction) == Approx(0.0f).margin(1e-4f));
    }
  }
  REQUIRE(fromApex >= 4);
}

TEST_CASE("only spot lights get a cone", "[editor][lights]") {
  const renderer::Light point{.type = renderer::LightType::Point};
  std::vector<renderer::DebugLine> lines;
  editor::appendSpotLightLines(std::span{&point, 1}, lines);
  REQUIRE(lines.empty());
}
