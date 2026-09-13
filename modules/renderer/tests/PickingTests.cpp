#include <sonnet/renderer/Picker.h>
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
#include <cstring>
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

SceneView boxScene(std::span<const DrawItem> draws) {
  SceneView view;
  view.camera.position = {0.0f, 0.0f, 3.0f};
  view.draws = draws;
  return view;
}

ImageDesc idImageDesc(glm::uvec2 size) {
  return {.size = size, .format = Renderer::IdFormat, .usage = ImageUsage::None, .debugName = "ids"};
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

TEST_CASE("the id and outline passes record over the scene and the picker copies on request",
          "[renderer][picking][null]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto device = createNullDevice();
  Renderer renderer{*device, shaderDir(platform)};
  Picker picker{*device};
  const MeshHandle box = renderer.createMesh(primitives::box(), "box");
  const std::array draws{DrawItem{.mesh = box, .id = 5}, DrawItem{.mesh = box, .id = 9}};
  const SceneView view = boxScene(draws);
  const std::array selected{std::uint32_t{9}};
  RenderGraph graph{*device};
  RenderTarget target{*device, "viewport"};
  target.resize({64, 32});

  const auto frame = [&](bool pick) {
    ICommandList &commands = device->beginFrame();
    const std::optional<std::uint32_t> answer = picker.poll();
    if (pick) {
      picker.request({10, 20});
    }
    graph.reset();
    const GraphImage color = graph.importImage(target.color());
    const GraphImage depth = graph.importImage(target.depth());
    const GraphImage ids = graph.createImage(idImageDesc(target.size()));
    renderer.addScenePasses(graph, view, color, depth);
    renderer.addIdPass(graph, view, ids, depth);
    renderer.addOutlinePass(graph, color, ids, selected);
    picker.addPass(graph, ids, target.size());
    graph.execute(commands);
    device->endFrame();
    return answer;
  };

  REQUIRE(!frame(true).has_value());
  REQUIRE(countLines(*device, "bindPipeline \"forward\"") == 1);
  REQUIRE(countLines(*device, "bindPipeline \"id\"") == 1);
  REQUIRE(countLines(*device, "bindPipeline \"outline\"") == 1);
  REQUIRE(countLines(*device, "drawIndexed 36 x1") == 4); // two boxes in the forward and the id pass
  REQUIRE(countLines(*device, "bindImage 2 \"ids\"") == 1);
  REQUIRE(countLines(*device, "draw 3 x1") == 1);
  REQUIRE(countLines(*device, "copyImageToBuffer \"ids\"") == 1);
  REQUIRE(countLines(*device, "barrier \"ids\" ColorAttachment->ShaderReadOnly") == 1);
  REQUIRE(renderer.statistics().drawCount == 2); // the id pass is not counted
  REQUIRE(graph.statistics().passes.size() == 4);

  // The answer arrives when the slot comes round, FramesInFlight frames later; the null device's
  // memory reads as zero.
  for (std::uint32_t i = 1; i < FramesInFlight; ++i) {
    REQUIRE(!frame(false).has_value());
  }
  const std::optional<std::uint32_t> answer = frame(false);
  REQUIRE(answer.has_value());
  REQUIRE(*answer == 0);
  REQUIRE(countLines(*device, "copyImageToBuffer") == 0);
  REQUIRE(!frame(false).has_value());

  renderer.destroyMesh(box);
}

TEST_CASE("an empty selection adds no outline pass", "[renderer][picking][null]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto device = createNullDevice();
  Renderer renderer{*device, shaderDir(platform)};
  RenderGraph graph{*device};
  RenderTarget target{*device, "viewport"};
  target.resize({8, 8});
  ICommandList &commands = device->beginFrame();
  graph.reset();
  const GraphImage ids = graph.createImage(idImageDesc(target.size()));
  renderer.addOutlinePass(graph, graph.importImage(target.color()), ids, {});
  graph.execute(commands);
  device->endFrame();
  REQUIRE(graph.statistics().passes.empty());
}

TEST_CASE("a box is picked by id and outlined on a GPU", "[renderer][picking][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device;
  try {
    device = createDevice({.platform = &platform, .applicationName = "renderer_tests"});
  } catch (const sonnet::core::Exception &e) {
    SKIP("no usable Vulkan 1.4 device: " << e.what());
  }
  {
    Renderer renderer{*device, shaderDir(platform)};
    Picker picker{*device};
    const MeshHandle box = renderer.createMesh(primitives::box(), "box");
    constexpr std::uint32_t BoxId = 42;
    const std::array draws{DrawItem{.mesh = box, .color = {0.2f, 0.2f, 0.2f, 1.0f}, .id = BoxId}};
    const SceneView view = boxScene(draws);
    constexpr glm::uvec2 size{64, 64};
    RenderGraph graph{*device};
    RenderTarget target{*device, "viewport"};
    target.resize(size);
    const BufferHandle readback = device->createBuffer({.size = std::uint64_t{size.x} * size.y * 4,
                                                        .usage = BufferUsage::TransferDst,
                                                        .memory = MemoryUsage::GpuToCpu,
                                                        .debugName = "readback"});
    const std::array selected{BoxId};
    // The near face, half a metre across at two and a half metres with a 60 degree field of view,
    // covers about eleven pixels of the 32 from the centre.
    constexpr unsigned centre = size.y / 2;
    constexpr unsigned justOutside = centre - 13;
    constexpr unsigned farOutside = centre - 20;

    std::optional<std::uint32_t> centreId;
    std::optional<std::uint32_t> outsideId;
    for (int frame = 0; frame < 4; ++frame) {
      ICommandList &commands = device->beginFrame();
      const std::optional<std::uint32_t> answer = picker.poll();
      if (frame == 0) {
        picker.request({size.x / 2, centre});
      } else if (frame == 1) {
        picker.request({size.x / 2, farOutside});
      }
      if (answer) {
        (frame == 2 ? centreId : outsideId) = answer;
      }
      graph.reset();
      const GraphImage color = graph.importImage(target.color());
      const GraphImage depth = graph.importImage(target.depth());
      const GraphImage ids = graph.createImage(idImageDesc(size));
      renderer.addScenePasses(graph, view, color, depth, {0.0f, 0.0f, 0.0f, 1.0f});
      renderer.addIdPass(graph, view, ids, depth);
      renderer.addOutlinePass(graph, color, ids, selected, {1.0f, 0.5f, 0.0f, 1.0f});
      picker.addPass(graph, ids, size);
      if (frame == 3) { // one readback, after the frames that could still be writing it have passed
        graph.addPass(
            "readback", [&](PassBuilder &b) { b.transferSrc(color); },
            [&](ICommandList &cmd, const PassResources &resources) {
              cmd.copyImageToBuffer(resources.image(color), readback);
            });
      }
      graph.execute(commands);
      device->endFrame();
    }
    device->waitIdle();

    REQUIRE(centreId.has_value());
    REQUIRE(*centreId == BoxId);
    REQUIRE(outsideId.has_value());
    REQUIRE(*outsideId == 0);

    const std::span<const std::byte> pixels = device->mappedRange(readback);
    const Pixel edge = pixelAt(pixels, size, size.x / 2, justOutside);
    REQUIRE(edge.r == 255);
    REQUIRE(edge.g > 100);
    REQUIRE(edge.b == 0);
    const Pixel inside = pixelAt(pixels, size, size.x / 2, centre);
    REQUIRE(inside.r < 200); // the grey box face, not the outline
    const Pixel far = pixelAt(pixels, size, size.x / 2, farOutside);
    REQUIRE(far.r + far.g + far.b == 0);
    REQUIRE(device->validationMessageCount() == 0);

    device->destroyBuffer(readback);
    renderer.destroyMesh(box);
  }
  REQUIRE(device->validationMessageCount() == 0);
}
