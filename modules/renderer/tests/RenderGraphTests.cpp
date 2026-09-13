#include <sonnet/renderer/RenderGraph.h>

#include <sonnet/rhi/NullDevice.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string_view>

using namespace sonnet::renderer;
using namespace sonnet::rhi;

namespace {

std::size_t countLines(const NullDevice &device, std::string_view text) {
  return static_cast<std::size_t>(
      std::ranges::count_if(device.trace(), [&](const std::string &line) { return line.contains(text); }));
}

std::size_t lineIndex(const NullDevice &device, std::string_view text) {
  const auto it = std::ranges::find_if(device.trace(), [&](const std::string &line) { return line.contains(text); });
  REQUIRE(it != device.trace().end());
  return static_cast<std::size_t>(it - device.trace().begin());
}

const ImageDesc SceneDesc{
    .size = {64, 64}, .format = Format::R8G8B8A8Unorm, .usage = ImageUsage::None, .debugName = "scene"};

} // namespace

TEST_CASE("a graphics pass gets its attachments transitioned and rendering opened", "[renderer][graph]") {
  const auto device = createNullDevice();
  RenderGraph graph{*device};
  int executed = 0;

  ICommandList &commands = device->beginFrame();
  graph.reset();
  const GraphImage color = graph.createImage(SceneDesc);
  const GraphImage depth = graph.createImage({.size = {64, 64}, .format = Format::D32Sfloat, .debugName = "depth"});
  graph.addPass(
      "scene",
      [&](PassBuilder &builder) {
        builder.color(color, LoadOp::Clear, {0.0f, 0.0f, 0.0f, 1.0f});
        builder.depth(depth, LoadOp::Clear);
      },
      [&](ICommandList &, const PassResources &resources) {
        ++executed;
        REQUIRE(device->isValid(resources.image(color)));
        REQUIRE(device->imageDesc(resources.image(color)).size == glm::uvec2{64, 64});
        // Usage comes from the declared use, not from the description the pass author wrote.
        REQUIRE(has(device->imageDesc(resources.image(color)).usage, ImageUsage::ColorAttachment));
        REQUIRE(has(device->imageDesc(resources.image(depth)).usage, ImageUsage::DepthAttachment));
      });
  graph.execute(commands);
  device->endFrame();

  REQUIRE(executed == 1);
  REQUIRE(countLines(*device, "barrier \"scene\" Undefined->ColorAttachment") == 1);
  REQUIRE(countLines(*device, "barrier \"depth\" Undefined->DepthAttachment") == 1);
  REQUIRE(lineIndex(*device, "barrier \"scene\"") < lineIndex(*device, "beginRendering"));
  REQUIRE(countLines(*device, "beginRendering color \"scene\" clear depth \"depth\" clear") == 1);
  REQUIRE(countLines(*device, "endRendering") == 1);
  REQUIRE(graph.statistics().passes.size() == 1);
  REQUIRE(graph.statistics().passes[0].name == "scene");
  REQUIRE(graph.statistics().barrierCount == 2);
}

TEST_CASE("a read after a write gets one barrier and a read after a read none", "[renderer][graph]") {
  const auto device = createNullDevice();
  RenderGraph graph{*device};
  ICommandList &commands = device->beginFrame();
  graph.reset();
  const GraphImage scene = graph.createImage(SceneDesc);
  const GraphImage output = graph.createImage({.size = {64, 64}, .debugName = "output"});
  graph.addPass("draw", [&](PassBuilder &b) { b.color(scene); }, {});
  graph.addPass("post",
                [&](PassBuilder &b) {
                  b.sample(scene);
                  b.color(output);
                },
                {});
  graph.addPass("overlay",
                [&](PassBuilder &b) {
                  b.sample(scene);
                  b.color(output, LoadOp::Load);
                },
                {});
  graph.execute(commands);
  device->endFrame();

  REQUIRE(countLines(*device, "barrier \"scene\" Undefined->ColorAttachment") == 1);
  REQUIRE(countLines(*device, "barrier \"scene\" ColorAttachment->ShaderReadOnly") == 1);
  REQUIRE(countLines(*device, "barrier \"scene\"") == 2);
  // Two consecutive writes to the same attachment are a hazard even with the layout unchanged.
  REQUIRE(countLines(*device, "barrier \"output\" ColorAttachment->ColorAttachment") == 1);
  REQUIRE(countLines(*device, "beginRendering color \"output\" load") == 1);
}

TEST_CASE("imported images end in their requested layout and transient ones are pooled", "[renderer][graph]") {
  const auto device = createNullDevice();
  RenderGraph graph{*device};
  const ImageHandle swapchainImage = device->createImage({.size = {32, 32},
                                                          .format = Format::B8G8R8A8Unorm,
                                                          .usage = ImageUsage::ColorAttachment,
                                                          .debugName = "backbuffer"});

  ImageHandle firstTransient;
  for (int frame = 0; frame < 3; ++frame) {
    ICommandList &commands = device->beginFrame();
    graph.reset();
    const GraphImage scene = graph.createImage(SceneDesc);
    const GraphImage back = graph.importImage(swapchainImage, ImageLayout::Present);
    graph.addPass("scene", [&](PassBuilder &b) { b.color(scene); }, {});
    graph.addPass(
        "composite",
        [&](PassBuilder &b) {
          b.sample(scene);
          b.color(back);
        },
        [&](ICommandList &, const PassResources &resources) {
          if (frame == 0) {
            firstTransient = resources.image(scene);
          } else {
            REQUIRE(resources.image(scene) == firstTransient); // reused, not reallocated
          }
        });
    graph.execute(commands);
    device->endFrame();
    REQUIRE(countLines(*device, "barrier \"backbuffer\" ColorAttachment->Present") == 1);
    REQUIRE(graph.statistics().transientImageCount == 1);
    REQUIRE(graph.statistics().transientImageBytes == 64 * 64 * 4);
  }
  device->destroyImage(swapchainImage);
}

TEST_CASE("transient images no frame uses any more are released", "[renderer][graph]") {
  const auto device = createNullDevice();
  RenderGraph graph{*device};
  {
    ICommandList &commands = device->beginFrame();
    graph.reset();
    const GraphImage scene = graph.createImage(SceneDesc);
    graph.addPass("scene", [&](PassBuilder &b) { b.color(scene); }, {});
    graph.execute(commands);
    device->endFrame();
  }
  REQUIRE(graph.statistics().transientImageCount == 1);
  for (int frame = 0; frame < 8; ++frame) {
    ICommandList &commands = device->beginFrame();
    graph.reset();
    graph.execute(commands);
    device->endFrame();
  }
  REQUIRE(graph.statistics().transientImageCount == 0);
}

TEST_CASE("pass timings are reported per pass with GPU times from the reused slot", "[renderer][graph]") {
  const auto device = createNullDevice();
  RenderGraph graph{*device};
  for (std::uint32_t frame = 0; frame < FramesInFlight + 1; ++frame) {
    ICommandList &commands = device->beginFrame();
    graph.reset();
    const GraphImage scene = graph.createImage(SceneDesc);
    graph.addPass("a", [&](PassBuilder &b) { b.color(scene); }, {});
    graph.addPass("b", [&](PassBuilder &b) { b.color(scene, LoadOp::Load); }, {});
    graph.execute(commands);
    REQUIRE(countLines(*device, "timestamp") == 4);
    device->endFrame();
  }
  const GraphStatistics &stats = graph.statistics();
  REQUIRE(stats.passes.size() == 2);
  REQUIRE(stats.passes[0].name == "a");
  REQUIRE(stats.passes[1].name == "b");
  REQUIRE(stats.passes[0].cpuMilliseconds >= 0.0f);
  REQUIRE(stats.passes[0].gpuMilliseconds == 0.0f); // the null device's clock never advances
}
