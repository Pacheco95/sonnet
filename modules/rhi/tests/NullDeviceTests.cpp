#include <sonnet/rhi/NullDevice.h>

#include <sonnet/platform/Platform.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstring>

using namespace sonnet::rhi;

namespace {

bool traceContains(const NullDevice &device, std::string_view text) {
  return std::ranges::any_of(device.trace(), [&](const std::string &line) { return line.contains(text); });
}

} // namespace

TEST_CASE("null device hands out handles that behave like the real ones", "[rhi][null]") {
  const auto device = createNullDevice();
  const BufferHandle upload = device->createBuffer(
      {.size = 64, .usage = BufferUsage::Storage, .memory = MemoryUsage::CpuToGpu, .debugName = "upload"});
  REQUIRE(device->isValid(upload));
  REQUIRE(device->mappedRange(upload).size() == 64);
  REQUIRE(device->bufferAddress(upload) != 0);
  const ImageHandle image = device->createImage(
      {.size = {8, 4}, .format = Format::D32Sfloat, .usage = ImageUsage::DepthAttachment, .debugName = "depth"});
  REQUIRE(device->imageDesc(image).size == glm::uvec2{8, 4});
  device->destroyBuffer(upload);
  REQUIRE(!device->isValid(upload));
  REQUIRE(device->mappedRange(upload).empty());
  device->destroyImage(image);
  REQUIRE(!device->isValid(image));
}

TEST_CASE("null device records a readable trace per frame", "[rhi][null]") {
  const auto device = createNullDevice();
  const ImageHandle color = device->createImage(
      {.size = {16, 16}, .format = Format::R8G8B8A8Unorm, .usage = ImageUsage::ColorAttachment, .debugName = "scene"});
  const ShaderHandle shader = device->createShader({.spirv = {}, .debugName = "shader"});
  const PipelineHandle pipeline =
      device->createGraphicsPipeline({.shader = shader, .colorFormats = {Format::R8G8B8A8Unorm}, .debugName = "lit"});

  ICommandList &commands = device->beginFrame();
  const ImageBarrier barrier{.image = color,
                             .srcStage = PipelineStage::AllCommands,
                             .oldLayout = ImageLayout::Undefined,
                             .dstStage = PipelineStage::ColorAttachmentOutput,
                             .dstAccess = Access::ColorAttachmentWrite,
                             .newLayout = ImageLayout::ColorAttachment};
  commands.barrier({&barrier, 1});
  const ColorAttachment attachment{.image = color};
  commands.beginRendering({.colors = {&attachment, 1}});
  commands.bindPipeline(pipeline);
  commands.draw(3);
  commands.endRendering();
  commands.writeTimestamp(1);
  REQUIRE(traceContains(*device, "barrier \"scene\" Undefined->ColorAttachment"));
  REQUIRE(traceContains(*device, "beginRendering color \"scene\" clear"));
  REQUIRE(traceContains(*device, "bindPipeline \"lit\""));
  REQUIRE(traceContains(*device, "draw 3 x1"));
  device->endFrame();

  static_cast<void>(device->beginFrame());
  REQUIRE(device->trace().empty()); // a new frame starts a new trace
  device->endFrame();

  device->destroyPipeline(pipeline);
  device->destroyShader(shader);
  device->destroyImage(color);
}

TEST_CASE("null device transient allocations and timestamps follow the frame slots", "[rhi][null]") {
  const auto device = createNullDevice();
  ICommandList &commands = device->beginFrame();
  const TransientAllocation a = device->allocateTransient(16);
  const TransientAllocation b = device->allocateTransient(16);
  REQUIRE(a.data.size() == 16);
  REQUIRE(b.offset == 256);
  std::memset(a.data.data(), 1, a.data.size());
  commands.writeTimestamp(0);
  commands.writeTimestamp(2);
  device->endFrame();
  for (std::uint32_t i = 1; i < FramesInFlight; ++i) {
    static_cast<void>(device->beginFrame());
    REQUIRE(device->timestamps().empty());
    device->endFrame();
  }
  static_cast<void>(device->beginFrame());
  REQUIRE(device->timestamps().size() == 3);
  REQUIRE(device->allocateTransient(16).offset == 0);
  device->endFrame();
}

TEST_CASE("null swapchain cycles through images of the window's size", "[rhi][null]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto window = platform.createWindow({.title = "null", .size = {64, 48}});
  const auto device = createNullDevice();
  const auto swapchain = device->createSwapchain(*window);
  REQUIRE(swapchain->extent() == glm::uvec2{64, 48});
  REQUIRE(swapchain->imageCount() == 3);
  std::array<std::uint32_t, 3> seen{};
  for (int frame = 0; frame < 3; ++frame) {
    static_cast<void>(device->beginFrame());
    const auto image = swapchain->acquire();
    REQUIRE(image.has_value());
    REQUIRE(device->isValid(image->image));
    seen[static_cast<std::size_t>(frame)] = image->index;
    device->endFrame();
  }
  REQUIRE(seen == std::array<std::uint32_t, 3>{0, 1, 2});
}

TEST_CASE("null device reports a memory budget from its live resources", "[rhi][null]") {
  const auto device = createNullDevice();
  const MemoryBudget before = device->memoryBudget();
  const ImageHandle image = device->createImage(
      {.size = {64, 64}, .format = Format::R8G8B8A8Unorm, .usage = ImageUsage::Sampled, .debugName = "texture"});
  const MemoryBudget after = device->memoryBudget();
  REQUIRE(after.heapCount == 1);
  REQUIRE(after.heaps[0].usage == before.heaps[0].usage + 64 * 64 * 4);
  device->destroyImage(image);
}
