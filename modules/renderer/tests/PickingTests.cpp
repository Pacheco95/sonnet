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
#include <vector>

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

// No shadows, bloom or anti-aliasing: the picking passes are what these tests count.
RendererSettings pickingSettings() {
  RendererSettings settings;
  settings.shadows = false;
  settings.bloom = false;
  settings.antialiasing = false;
  return settings;
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
  Renderer renderer{*device, shaderDir(platform), pickingSettings()};
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
    ImageDesc maskDesc = idImageDesc(target.size());
    maskDesc.debugName = "mask";
    const GraphImage mask = graph.createImage(maskDesc);
    renderer.addScenePasses(graph, view, color, depth);
    renderer.addIdPass(graph, view, ids, depth);
    renderer.addSelectionMaskPass(graph, view, mask, selected);
    renderer.addOutlinePass(graph, color, mask);
    picker.addPass(graph, ids, target.size());
    graph.execute(commands);
    device->endFrame();
    return answer;
  };

  REQUIRE(!frame(true).has_value());
  REQUIRE(countLines(*device, "bindPipeline \"forward\"") == 1);
  REQUIRE(countLines(*device, "bindPipeline \"id\"") == 1);
  REQUIRE(countLines(*device, "bindPipeline \"selection mask\"") == 1);
  REQUIRE(countLines(*device, "bindPipeline \"outline\"") == 1);
  // Two boxes in the pre-pass, the forward and the id pass, the selected one in the mask pass.
  REQUIRE(countLines(*device, "drawIndexed 36 x1") == 7);
  REQUIRE(countLines(*device, "beginRendering color \"mask\" clear") == 1); // no depth attachment
  REQUIRE(countLines(*device, "bindImage 2 \"mask\"") == 1);
  REQUIRE(countLines(*device, "draw 3 x1") == 2); // the tone mapping and the outline
  REQUIRE(countLines(*device, "copyImageToBuffer \"ids\"") == 1);
  REQUIRE(countLines(*device, "barrier \"mask\" ColorAttachment->ShaderReadOnly") == 1);
  REQUIRE(renderer.statistics().drawCount == 2); // the id and mask passes are not counted
  // The lookup table, depth, clustering, forward, tonemap, id, mask, outline and the readback.
  REQUIRE(graph.statistics().passes.size() == 9);

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

TEST_CASE("the selection mask draws every listed item, given in any order with repeats", "[renderer][picking][null]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto device = createNullDevice();
  Renderer renderer{*device, shaderDir(platform), pickingSettings()};
  const MeshHandle box = renderer.createMesh(primitives::box(), "box");
  // Forty items, a selected parent's subtree, listed backwards with one id twice.
  std::vector<DrawItem> draws;
  std::vector<std::uint32_t> selected;
  for (std::uint32_t id = 1; id <= 40; ++id) {
    draws.push_back({.mesh = box, .id = id});
    if (id % 2 == 0) {
      selected.insert(selected.begin(), id);
    }
  }
  selected.push_back(4);
  const SceneView view = boxScene(draws);
  ICommandList &commands = device->beginFrame();
  RenderGraph graph{*device};
  RenderTarget target{*device, "viewport"};
  target.resize({64, 32});
  ImageDesc maskDesc = idImageDesc(target.size());
  maskDesc.debugName = "mask";
  const GraphImage mask = graph.createImage(maskDesc);
  renderer.addSelectionMaskPass(graph, view, mask, selected);
  renderer.addOutlinePass(graph, graph.importImage(target.color()), mask);
  graph.execute(commands);
  device->endFrame();
  REQUIRE(countLines(*device, "bindPipeline \"selection mask\"") == 1);
  REQUIRE(countLines(*device, "drawIndexed 36 x1") == 20); // each even id once, the odd ones never
  REQUIRE(countLines(*device, "bindPipeline \"outline\"") == 1);
  renderer.destroyMesh(box);
}

TEST_CASE("an empty selection adds no mask or outline pass", "[renderer][picking][null]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto device = createNullDevice();
  Renderer renderer{*device, shaderDir(platform), pickingSettings()};
  const MeshHandle box = renderer.createMesh(primitives::box(), "box");
  RenderGraph graph{*device};
  RenderTarget target{*device, "viewport"};
  target.resize({8, 8});
  ICommandList &commands = device->beginFrame();
  graph.reset();
  const GraphImage mask = graph.createImage(idImageDesc(target.size()));
  const std::array draws{DrawItem{.mesh = box, .id = 1}};
  const SceneView view = boxScene(draws);
  renderer.addSelectionMaskPass(graph, view, mask, {});
  renderer.addOutlinePass(graph, graph.importImage(target.color()), mask);
  graph.execute(commands);
  device->endFrame();
  REQUIRE(graph.statistics().passes.empty());
  renderer.destroyMesh(box);
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
    Renderer renderer{*device, shaderDir(platform), pickingSettings()};
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
      const GraphImage mask = graph.createImage(idImageDesc(size));
      renderer.addScenePasses(graph, view, color, depth, {0.0f, 0.0f, 0.0f, 1.0f});
      renderer.addIdPass(graph, view, ids, depth);
      renderer.addSelectionMaskPass(graph, view, mask, selected);
      renderer.addOutlinePass(graph, color, mask, {1.0f, 0.5f, 0.0f, 1.0f});
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

TEST_CASE("an occluder in front of a selected surface is not outlined", "[renderer][picking][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device;
  try {
    device = createDevice({.platform = &platform, .applicationName = "renderer_tests"});
  } catch (const sonnet::core::Exception &e) {
    SKIP("no usable Vulkan 1.4 device: " << e.what());
  }
  {
    Renderer renderer{*device, shaderDir(platform), pickingSettings()};
    const MeshHandle box = renderer.createMesh(primitives::box(), "box");
    const MeshHandle plane = renderer.createMesh(primitives::plane({10.0f, 10.0f}), "plane");
    constexpr std::uint32_t PlaneId = 3;
    // The plane turned to face the camera, one metre behind the box, filling the whole view: its
    // silhouette has no edge on screen, and the box in front must not carve one into it.
    const glm::mat4 facing = glm::translate(glm::mat4{1.0f}, {0.0f, 0.0f, -1.0f}) *
                             glm::rotate(glm::mat4{1.0f}, glm::radians(90.0f), glm::vec3{1.0f, 0.0f, 0.0f});
    const std::array draws{
        DrawItem{.mesh = box, .color = {0.2f, 0.2f, 0.2f, 1.0f}, .id = 42},
        DrawItem{.mesh = plane, .transform = facing, .color = {0.5f, 0.5f, 0.5f, 1.0f}, .id = PlaneId}};
    const SceneView view = boxScene(draws);
    constexpr glm::uvec2 size{64, 64};
    RenderGraph graph{*device};
    RenderTarget target{*device, "viewport"};
    target.resize(size);
    const BufferHandle readback = device->createBuffer({.size = std::uint64_t{size.x} * size.y * 4,
                                                        .usage = BufferUsage::TransferDst,
                                                        .memory = MemoryUsage::GpuToCpu,
                                                        .debugName = "readback"});
    const std::array selected{PlaneId};

    ICommandList &commands = device->beginFrame();
    graph.reset();
    const GraphImage color = graph.importImage(target.color());
    const GraphImage depth = graph.importImage(target.depth());
    const GraphImage mask = graph.createImage(idImageDesc(size));
    renderer.addScenePasses(graph, view, color, depth, {0.0f, 0.0f, 0.0f, 1.0f});
    renderer.addSelectionMaskPass(graph, view, mask, selected);
    renderer.addOutlinePass(graph, color, mask, {1.0f, 0.5f, 0.0f, 1.0f});
    graph.addPass(
        "readback", [&](PassBuilder &b) { b.transferSrc(color); },
        [&](ICommandList &cmd, const PassResources &resources) {
          cmd.copyImageToBuffer(resources.image(color), readback);
        });
    graph.execute(commands);
    device->endFrame();
    device->waitIdle();

    const std::span<const std::byte> pixels = device->mappedRange(readback);
    // Nothing on screen is the outline colour: not the box's edge, not the plane around it.
    for (unsigned y = 0; y < size.y; ++y) {
      for (unsigned x = 0; x < size.x; ++x) {
        const Pixel pixel = pixelAt(pixels, size, x, y);
        REQUIRE(!(pixel.r == 255 && pixel.b == 0 && pixel.g > 100 && pixel.g < 160));
      }
    }
    const Pixel centre = pixelAt(pixels, size, size.x / 2, size.y / 2);
    REQUIRE(centre.r < 200); // the box, drawn in front of the plane
    REQUIRE(device->validationMessageCount() == 0);

    device->destroyBuffer(readback);
    renderer.destroyMesh(plane);
    renderer.destroyMesh(box);
  }
  REQUIRE(device->validationMessageCount() == 0);
}
