#include "TestDevice.h"

#include <sonnet/core/File.h>
#include <sonnet/rhi/Device.h>
#include <sonnet/rhi/NullDevice.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <memory>

using namespace sonnet::rhi;

namespace {

// Matches indirect.slang under scalar block layout.
struct Object {
  glm::vec4 color;
  glm::vec4 offset;
};

struct PushConstants {
  std::uint64_t commands;
  std::uint64_t count;
  std::uint64_t vertices;
  std::uint32_t objectCount;
  std::uint32_t indexCount;
  std::uint32_t keepMask;
};
static_assert(sizeof(PushConstants) <= PushConstantSize);

constexpr std::uint32_t ObjectCount = 3;
constexpr glm::uvec2 Size{48, 48};

struct Pixel {
  int r, g, b, a;
};

Pixel pixelAt(std::span<const std::byte> pixels, unsigned x, unsigned y) {
  const std::size_t offset = (std::size_t{y} * Size.x + x) * 4;
  return {std::to_integer<int>(pixels[offset]), std::to_integer<int>(pixels[offset + 1]),
          std::to_integer<int>(pixels[offset + 2]), std::to_integer<int>(pixels[offset + 3])};
}

// Three tall triangles side by side, one per object, drawn from commands a compute shader
// writes. `keepMask` decides which objects survive, as a frustum test would.
struct IndirectScene {
  test::TestDevice &device;
  ShaderHandle shader;
  PipelineHandle buildPipeline;
  PipelineHandle slotsPipeline;
  PipelineHandle drawPipeline;
  BufferHandle vertices;
  BufferHandle indices;
  BufferHandle commands; // the commands, then the count in its own buffer
  BufferHandle counts;
  ImageHandle color;
  BufferHandle readback;

  explicit IndirectScene(test::TestDevice &testDevice) : device(testDevice) {
    const auto spirv = sonnet::core::readFile(device.platform.basePath() / "shaders" / "indirect.spv");
    REQUIRE(spirv.has_value());
    shader = device->createShader({.spirv = *spirv, .debugName = "indirect"});
    buildPipeline =
        device->createComputePipeline({.shader = shader, .entry = "buildCommands", .debugName = "build commands"});
    slotsPipeline =
        device->createComputePipeline({.shader = shader, .entry = "buildSlots", .debugName = "build slots"});
    drawPipeline = device->createGraphicsPipeline({.shader = shader,
                                                   .colorFormats = {Format::R8G8B8A8Unorm},
                                                   .cullMode = CullMode::None,
                                                   .debugName = "indirect draw"});

    const std::array<glm::vec3, 3> vertexData{{{-0.3f, -0.9f, 0.5f}, {0.3f, -0.9f, 0.5f}, {0.0f, 0.9f, 0.5f}}};
    const std::array<std::uint32_t, 3> indexData{0, 1, 2};
    vertices = device->createBuffer({.size = sizeof(vertexData),
                                     .usage = BufferUsage::Storage,
                                     .memory = MemoryUsage::CpuToGpu,
                                     .debugName = "indirect vertices"});
    std::memcpy(device->mappedRange(vertices).data(), vertexData.data(), sizeof(vertexData));
    indices = device->createBuffer({.size = sizeof(indexData),
                                    .usage = BufferUsage::Index,
                                    .memory = MemoryUsage::CpuToGpu,
                                    .debugName = "indirect indices"});
    std::memcpy(device->mappedRange(indices).data(), indexData.data(), sizeof(indexData));
    // Device-local, written by compute and read by the draw: what the renderer's own command
    // and count buffers are.
    commands = device->createBuffer({.size = ObjectCount * sizeof(IndirectCommand),
                                     .usage = BufferUsage::Storage | BufferUsage::Indirect,
                                     .debugName = "draw commands"});
    counts = device->createBuffer({.size = sizeof(std::uint32_t),
                                   .usage = BufferUsage::Storage | BufferUsage::Indirect,
                                   .debugName = "draw counts"});
    color = device->createImage({.size = Size,
                                 .format = Format::R8G8B8A8Unorm,
                                 .usage = ImageUsage::ColorAttachment | ImageUsage::TransferSrc,
                                 .debugName = "indirect color"});
    readback = device->createBuffer({.size = std::uint64_t{Size.x} * Size.y * 4,
                                     .usage = BufferUsage::TransferDst,
                                     .memory = MemoryUsage::GpuToCpu,
                                     .debugName = "readback"});
  }

  ~IndirectScene() {
    device->waitIdle();
    device->destroyBuffer(readback);
    device->destroyImage(color);
    device->destroyBuffer(counts);
    device->destroyBuffer(commands);
    device->destroyBuffer(indices);
    device->destroyBuffer(vertices);
    device->destroyPipeline(drawPipeline);
    device->destroyPipeline(slotsPipeline);
    device->destroyPipeline(buildPipeline);
    device->destroyShader(shader);
  }

  // `counted` builds packed commands and a count for drawIndexedIndirectCount; otherwise one
  // slot per object, drawn whole by drawIndexedIndirect with `maxDrawCount` as the draw count.
  void drawFrame(std::uint32_t keepMask, std::uint32_t maxDrawCount, bool counted = true) {
    ICommandList &cmd = device->beginFrame();
    const TransientAllocation objects = device->allocateTransient(ObjectCount * sizeof(Object));
    REQUIRE(objects.data.size() == ObjectCount * sizeof(Object));
    const std::array<Object, ObjectCount> objectData{{
        {{1.0f, 0.0f, 0.0f, 1.0f}, {-0.65f, 0.0f, 0.0f, 0.0f}},
        {{0.0f, 1.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}},
        {{0.0f, 0.0f, 1.0f, 1.0f}, {0.65f, 0.0f, 0.0f, 0.0f}},
    }};
    std::memcpy(objects.data.data(), objectData.data(), objects.data.size());
    const std::array binding{BufferBinding{.binding = PassStorageBinding,
                                           .buffer = objects.buffer,
                                           .offset = objects.offset,
                                           .size = objects.data.size()}};

    PushConstants push{.commands = device->bufferAddress(commands),
                       .count = device->bufferAddress(counts),
                       .vertices = device->bufferAddress(vertices),
                       .objectCount = ObjectCount,
                       .indexCount = 3,
                       .keepMask = keepMask};
    cmd.bindPipeline(counted ? buildPipeline : slotsPipeline);
    cmd.bindBuffers(binding);
    cmd.pushConstants(std::as_bytes(std::span{&push, 1}));
    cmd.dispatch(1, 1, 1);
    // The compute writes have to be visible to the command fetch, which is its own stage.
    cmd.memoryBarrier({.srcStage = PipelineStage::ComputeShader,
                       .srcAccess = Access::ShaderWrite,
                       .dstStage = PipelineStage::DrawIndirect,
                       .dstAccess = Access::IndirectCommandRead});

    test::transition(cmd, test::toColorAttachment(color));
    const ColorAttachment attachment{.image = color, .clearColor = {0.0f, 0.0f, 0.0f, 1.0f}};
    cmd.beginRendering({.colors = {&attachment, 1}});
    cmd.bindPipeline(drawPipeline);
    cmd.bindBuffers(binding);
    cmd.pushConstants(std::as_bytes(std::span{&push, 1}));
    cmd.bindIndexBuffer(indices, IndexType::Uint32);
    if (counted) {
      cmd.drawIndexedIndirectCount(commands, 0, counts, 0, maxDrawCount);
    } else {
      cmd.drawIndexedIndirect(commands, 0, maxDrawCount);
    }
    cmd.endRendering();
    test::transition(cmd, test::colorToTransferSrc(color));
    cmd.copyImageToBuffer(color, readback);
    device->endFrame();
  }

  // The pixel over each object's triangle: left, centre, right at mid height.
  std::array<Pixel, ObjectCount> columns() {
    device->waitIdle();
    const std::span<const std::byte> pixels = device->mappedRange(readback);
    return {pixelAt(pixels, 8, Size.y / 2), pixelAt(pixels, 24, Size.y / 2), pixelAt(pixels, 39, Size.y / 2)};
  }
};

} // namespace

TEST_CASE("a compute pass writes the commands an indirect draw submits", "[rhi][indirect][gpu]") {
  test::TestDevice device;
  IndirectScene scene{device};
  // Objects 0 and 2 survive; the count buffer, not maxDrawCount, decides how many draw.
  scene.drawFrame(0b101, ObjectCount);
  const std::array<Pixel, ObjectCount> columns = scene.columns();
  REQUIRE(columns[0].r == 255);
  REQUIRE(columns[0].b == 0);
  REQUIRE(columns[1].g == 0); // culled: the clear colour stays
  REQUIRE(columns[1].r == 0);
  REQUIRE(columns[2].b == 255);
  REQUIRE(columns[2].r == 0);
}

TEST_CASE("the count buffer bounds an indirect draw below maxDrawCount", "[rhi][indirect][gpu]") {
  test::TestDevice device;
  IndirectScene scene{device};
  // Every object survives the build, but the draw is allowed only the first command.
  scene.drawFrame(0b111, 1);
  const std::array<Pixel, ObjectCount> columns = scene.columns();
  REQUIRE(columns[0].r == 255);
  REQUIRE(columns[1].g == 0);
  REQUIRE(columns[2].b == 0);
}

TEST_CASE("zero-instance slots stand in for the count without drawIndirectCount", "[rhi][indirect][gpu]") {
  test::TestDevice device{true};
  REQUIRE_FALSE(device->info().drawIndirectCountSupported);
  IndirectScene scene{device};
  // Every slot is drawn; the culled object's slot carries no instances (ADR-0014).
  scene.drawFrame(0b101, ObjectCount, false);
  const std::array<Pixel, ObjectCount> columns = scene.columns();
  REQUIRE(columns[0].r == 255);
  REQUIRE(columns[1].g == 0);
  REQUIRE(columns[1].r == 0);
  REQUIRE(columns[2].b == 255);
}

TEST_CASE("a device reports drawIndirectCount unless told to leave it off", "[rhi][indirect][gpu]") {
  {
    test::TestDevice device;
    // Every desktop driver and Lavapipe have it; only MoltenVK does not.
    REQUIRE(device->info().drawIndirectCountSupported);
  }
  test::TestDevice device{true};
  REQUIRE_FALSE(device->info().drawIndirectCountSupported);
}

TEST_CASE("the null device traces an indirect draw with the commands it was offered", "[rhi][indirect][null]") {
  const std::unique_ptr<NullDevice> device = createNullDevice();
  const BufferHandle commands =
      device->createBuffer({.size = 64, .usage = BufferUsage::Storage | BufferUsage::Indirect, .debugName = "cmds"});
  const BufferHandle counts =
      device->createBuffer({.size = 4, .usage = BufferUsage::Storage | BufferUsage::Indirect, .debugName = "counts"});
  ICommandList &cmd = device->beginFrame();
  const ImageHandle target = device->createImage(
      {.size = {4, 4}, .format = Format::R8G8B8A8Unorm, .usage = ImageUsage::ColorAttachment, .debugName = "target"});
  const ColorAttachment attachment{.image = target};
  cmd.beginRendering({.colors = {&attachment, 1}});
  cmd.drawIndexedIndirectCount(commands, 0, counts, 0, 7);
  cmd.endRendering();
  device->endFrame();
  REQUIRE(std::ranges::count(device->trace(), "drawIndexedIndirectCount \"cmds\" max 7") == 1);
  device->destroyImage(target);
  device->destroyBuffer(counts);
  device->destroyBuffer(commands);
}

TEST_CASE("the null device traces an uncounted indirect draw", "[rhi][indirect][null]") {
  const std::unique_ptr<NullDevice> device = createNullDevice();
  device->disableDrawIndirectCount();
  REQUIRE_FALSE(device->info().drawIndirectCountSupported);
  const BufferHandle commands =
      device->createBuffer({.size = 64, .usage = BufferUsage::Storage | BufferUsage::Indirect, .debugName = "cmds"});
  ICommandList &cmd = device->beginFrame();
  const ImageHandle target = device->createImage(
      {.size = {4, 4}, .format = Format::R8G8B8A8Unorm, .usage = ImageUsage::ColorAttachment, .debugName = "target"});
  const ColorAttachment attachment{.image = target};
  cmd.beginRendering({.colors = {&attachment, 1}});
  cmd.drawIndexedIndirect(commands, 0, 3);
  cmd.endRendering();
  device->endFrame();
  REQUIRE(std::ranges::count(device->trace(), "drawIndexedIndirect \"cmds\" count 3") == 1);
  device->destroyImage(target);
  device->destroyBuffer(commands);
}
