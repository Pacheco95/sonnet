#include <sonnet/editor/PrimitiveScene.h>

#include <sonnet/platform/Platform.h>
#include <sonnet/renderer/Renderer.h>
#include <sonnet/rhi/NullDevice.h>

#include <catch2/catch_test_macros.hpp>

using namespace sonnet;

TEST_CASE("the primitive scene owns one mesh per primitive and animates", "[editor][scene]") {
  platform::Platform platform{{.headless = true}};
  const auto device = rhi::createNullDevice();
  renderer::Renderer renderer{*device, platform.basePath() / "shaders"};
  {
    editor::PrimitiveScene scene{renderer};
    REQUIRE(scene.draws().size() == 6);
    for (const renderer::DrawItem &item : scene.draws()) {
      REQUIRE(renderer.isValid(item.mesh));
    }
    const glm::mat4 before = scene.draws()[5].transform;
    scene.update(0.5f);
    REQUIRE(scene.draws()[5].transform != before);
    REQUIRE(scene.draws()[1].transform == glm::translate(glm::mat4{1.0f}, {0.0f, 0.5f, 0.0f}));
  }
  // Meshes are released with the scene; the renderer reports nothing leaked at destruction.
}
