#include <sonnet/renderer/Primitives.h>
#include <sonnet/renderer/RenderGraph.h>
#include <sonnet/renderer/RenderTarget.h>
#include <sonnet/renderer/Renderer.h>

#include <sonnet/core/Error.h>
#include <sonnet/platform/Platform.h>
#include <sonnet/rhi/Device.h>
#include <sonnet/rhi/NullDevice.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <memory>
#include <string_view>

using namespace sonnet::renderer;
using namespace sonnet::rhi;

namespace {

std::size_t countLines(const NullDevice &device, std::string_view text) {
  return static_cast<std::size_t>(
      std::ranges::count_if(device.trace(), [&](const std::string &line) { return line.contains(text); }));
}

std::filesystem::path shaderDir(sonnet::platform::Platform &platform) {
  return platform.basePath() / "shaders";
}

// A camera three metres back looking at the origin, and a box there.
SceneView boxScene(std::span<const DrawItem> draws) {
  SceneView view;
  view.camera.position = {0.0f, 0.0f, 3.0f};
  view.draws = draws;
  return view;
}

struct Pixel {
  int r, g, b, a;
};

Pixel pixelAt(std::span<const std::byte> pixels, glm::uvec2 size, unsigned x, unsigned y) {
  const std::size_t offset = (std::size_t{y} * size.x + x) * 4;
  return {std::to_integer<int>(pixels[offset]), std::to_integer<int>(pixels[offset + 1]),
          std::to_integer<int>(pixels[offset + 2]), std::to_integer<int>(pixels[offset + 3])};
}

} // namespace

TEST_CASE("the renderer records one indexed draw per item into the scene pass", "[renderer][null]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto device = createNullDevice();
  Renderer renderer{*device, shaderDir(platform)};
  const MeshHandle box = renderer.createMesh(primitives::box(), "box");
  const MeshHandle sphere = renderer.createMesh(primitives::sphere(0.5f, 8, 4), "sphere");
  REQUIRE(renderer.isValid(box));
  const std::array draws{DrawItem{.mesh = box}, DrawItem{.mesh = sphere}, DrawItem{.mesh = box}};
  const SceneView view = boxScene(draws);

  RenderGraph graph{*device};
  RenderTarget target{*device, "viewport"};
  target.resize({128, 64});
  ICommandList &commands = device->beginFrame();
  graph.reset();
  renderer.addScenePasses(graph, view, graph.importImage(target.color()), graph.importImage(target.depth()));
  graph.execute(commands);
  device->endFrame();

  REQUIRE(countLines(*device, "bindPipeline \"forward\"") == 1);
  REQUIRE(countLines(*device, "bindBuffer 0") == 1);
  REQUIRE(countLines(*device, "bindBuffer 1") == 1);
  REQUIRE(countLines(*device, "drawIndexed 36 x1") == 2);
  REQUIRE(countLines(*device, "drawIndexed") == 3);
  REQUIRE(renderer.statistics().drawCount == 3);
  REQUIRE(renderer.statistics().triangleCount == 12 * 2 + (8 * 2 * 2 + 8 * 2));

  renderer.destroyMesh(sphere);
  renderer.destroyMesh(box);
  REQUIRE(!renderer.isValid(box));
}

TEST_CASE("a stale mesh handle draws nothing and an empty scene records no draw", "[renderer][null]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto device = createNullDevice();
  Renderer renderer{*device, shaderDir(platform)};
  const MeshHandle box = renderer.createMesh(primitives::box(), "box");
  renderer.destroyMesh(box);
  const std::array draws{DrawItem{.mesh = box}};
  const SceneView view = boxScene(draws);

  RenderGraph graph{*device};
  RenderTarget target{*device, "viewport"};
  target.resize({8, 8});
  ICommandList &commands = device->beginFrame();
  graph.reset();
  renderer.addScenePasses(graph, view, graph.importImage(target.color()), graph.importImage(target.depth()));
  graph.execute(commands);
  device->endFrame();
  REQUIRE(countLines(*device, "drawIndexed") == 0);
  REQUIRE(renderer.statistics().drawCount == 0);
}

TEST_CASE("a render target is recreated on resize and released at zero", "[renderer][null]") {
  const auto device = createNullDevice();
  RenderTarget target{*device, "viewport"};
  REQUIRE(!target.isValid());
  target.resize({320, 200});
  const ImageHandle first = target.color();
  REQUIRE(device->imageDesc(first).size == glm::uvec2{320, 200});
  REQUIRE(device->imageDesc(target.depth()).format == Renderer::DepthFormat);
  target.resize({320, 200});
  REQUIRE(target.color() == first); // same size, nothing recreated
  target.resize({64, 64});
  REQUIRE(!device->isValid(first));
  REQUIRE(target.size() == glm::uvec2{64, 64});
  target.resize({0, 0});
  REQUIRE(!target.isValid());
}

TEST_CASE("a lit box renders into the viewport target on a GPU", "[renderer][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device;
  try {
    device = createDevice({.platform = &platform, .applicationName = "renderer_tests"});
  } catch (const sonnet::core::Exception &e) {
    SKIP("no usable Vulkan 1.4 device: " << e.what());
  }
  {
    Renderer renderer{*device, shaderDir(platform)};
    const MeshHandle box = renderer.createMesh(primitives::box(), "box");
    const std::array draws{DrawItem{.mesh = box, .color = {1.0f, 0.5f, 0.2f, 1.0f}}};
    const SceneView view = boxScene(draws);
    constexpr glm::uvec2 size{64, 64};
    RenderGraph graph{*device};
    RenderTarget target{*device, "viewport"};
    target.resize(size);
    const BufferHandle readback = device->createBuffer({.size = std::uint64_t{size.x} * size.y * 4,
                                                        .usage = BufferUsage::TransferDst,
                                                        .memory = MemoryUsage::GpuToCpu,
                                                        .debugName = "readback"});

    ICommandList &commands = device->beginFrame();
    graph.reset();
    const GraphImage color = graph.importImage(target.color());
    renderer.addScenePasses(graph, view, color, graph.importImage(target.depth()), {0.0f, 0.0f, 0.0f, 1.0f});
    graph.addPass(
        "readback", [&](PassBuilder &b) { b.transferSrc(color); },
        [&](ICommandList &cmd, const PassResources &resources) {
          cmd.copyImageToBuffer(resources.image(color), readback);
        });
    graph.execute(commands);
    device->endFrame();
    device->waitIdle();

    const std::span<const std::byte> pixels = device->mappedRange(readback);
    const Pixel centre = pixelAt(pixels, size, size.x / 2, size.y / 2);
    // The lit box face is orange-ish: red dominates, and it is not the black clear colour.
    REQUIRE(centre.r > 60);
    REQUIRE(centre.r > centre.b);
    REQUIRE(centre.a == 255);
    const Pixel corner = pixelAt(pixels, size, 0, 0);
    REQUIRE(corner.r + corner.g + corner.b == 0);
    REQUIRE(renderer.statistics().drawCount == 1);
    REQUIRE(graph.statistics().passes.size() == 2);
    REQUIRE(device->validationMessageCount() == 0);

    device->destroyBuffer(readback);
    renderer.destroyMesh(box);
  }
  REQUIRE(device->validationMessageCount() == 0);
}
