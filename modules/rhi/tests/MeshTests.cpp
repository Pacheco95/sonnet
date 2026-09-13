#include "TestDevice.h"

#include <sonnet/core/File.h>
#include <sonnet/rhi/Device.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstring>
#include <vector>

using namespace sonnet::rhi;

namespace {

// Matches mesh.slang under scalar block layout: two float3 packed to 24 bytes.
struct Vertex {
  glm::vec3 position;
  glm::vec3 color;
};
static_assert(sizeof(Vertex) == 24);

struct PushConstants {
  std::uint64_t vertices;
  glm::vec4 tint;
};
static_assert(sizeof(PushConstants) == 24);

struct Pixel {
  int r, g, b, a;
};

Pixel pixelAt(std::span<const std::byte> pixels, glm::uvec2 size, unsigned x, unsigned y) {
  const std::size_t offset = (std::size_t{y} * size.x + x) * 4;
  return {std::to_integer<int>(pixels[offset]), std::to_integer<int>(pixels[offset + 1]),
          std::to_integer<int>(pixels[offset + 2]), std::to_integer<int>(pixels[offset + 3])};
}

// Two triangles covering the centre: a red one nearer (reversed-Z: greater depth) drawn first,
// a blue one farther drawn second. With the depth test on, red stays; without it, blue wins.
struct MeshScene {
  test::TestDevice &device;
  glm::uvec2 size{32, 32};
  ShaderHandle shader;
  BufferHandle vertices;
  BufferHandle indices;
  ImageHandle color;
  ImageHandle depth;
  BufferHandle readback;

  explicit MeshScene(test::TestDevice &testDevice) : device(testDevice) {
    const auto spirv = sonnet::core::readFile(device.platform.basePath() / "shaders" / "mesh.spv");
    REQUIRE(spirv.has_value());
    shader = device->createShader({.spirv = *spirv, .debugName = "mesh"});

    const std::array<Vertex, 6> vertexData{{
        {{0.0f, 0.8f, 0.5f}, {1.0f, 0.0f, 0.0f}},
        {{-0.8f, -0.8f, 0.5f}, {1.0f, 0.0f, 0.0f}},
        {{0.8f, -0.8f, 0.5f}, {1.0f, 0.0f, 0.0f}},
        {{0.0f, 0.8f, 0.2f}, {0.0f, 0.0f, 1.0f}},
        {{-0.8f, -0.8f, 0.2f}, {0.0f, 0.0f, 1.0f}},
        {{0.8f, -0.8f, 0.2f}, {0.0f, 0.0f, 1.0f}},
    }};
    const std::array<std::uint16_t, 6> indexData{0, 1, 2, 3, 4, 5};
    vertices = device->createBuffer({.size = sizeof(vertexData),
                                     .usage = BufferUsage::Storage,
                                     .memory = MemoryUsage::CpuToGpu,
                                     .debugName = "mesh vertices"});
    std::memcpy(device->mappedRange(vertices).data(), vertexData.data(), sizeof(vertexData));
    indices = device->createBuffer({.size = sizeof(indexData),
                                    .usage = BufferUsage::Index,
                                    .memory = MemoryUsage::CpuToGpu,
                                    .debugName = "mesh indices"});
    std::memcpy(device->mappedRange(indices).data(), indexData.data(), sizeof(indexData));
    color = device->createImage({.size = size,
                                 .format = Format::R8G8B8A8Unorm,
                                 .usage = ImageUsage::ColorAttachment | ImageUsage::TransferSrc,
                                 .debugName = "mesh color"});
    depth = device->createImage(
        {.size = size, .format = Format::D32Sfloat, .usage = ImageUsage::DepthAttachment, .debugName = "mesh depth"});
    readback = device->createBuffer({.size = std::uint64_t{size.x} * size.y * 4,
                                     .usage = BufferUsage::TransferDst,
                                     .memory = MemoryUsage::GpuToCpu,
                                     .debugName = "readback"});
  }

  ~MeshScene() {
    device->waitIdle();
    device->destroyBuffer(readback);
    device->destroyImage(depth);
    device->destroyImage(color);
    device->destroyBuffer(indices);
    device->destroyBuffer(vertices);
    device->destroyShader(shader);
  }

  PipelineHandle makePipeline(bool depthTest) {
    return device->createGraphicsPipeline({.shader = shader,
                                           .colorFormats = {Format::R8G8B8A8Unorm},
                                           .depthFormat = Format::D32Sfloat,
                                           .depth = {.test = depthTest, .write = depthTest},
                                           .cullMode = CullMode::None,
                                           .debugName = depthTest ? "mesh depth tested" : "mesh untested"});
  }

  // Records one frame drawing both triangles and copying the colour target into the readback.
  void drawFrame(PipelineHandle pipeline, bool withTimestamps) {
    ICommandList &commands = device->beginFrame();
    if (withTimestamps) {
      commands.writeTimestamp(0);
    }
    const TransientAllocation passConstants = device->allocateTransient(sizeof(glm::vec4));
    const TransientAllocation scales = device->allocateTransient(sizeof(glm::vec4));
    REQUIRE(passConstants.data.size() == sizeof(glm::vec4));
    REQUIRE(scales.data.size() == sizeof(glm::vec4));
    REQUIRE(scales.offset % 16 == 0); // the device aligns to its uniform and storage minimums
    const glm::vec4 offset{0.0f, 0.0f, 0.0f, 0.0f};
    const glm::vec4 scale{1.0f, 1.0f, 1.0f, 1.0f};
    std::memcpy(passConstants.data.data(), &offset, sizeof(offset));
    std::memcpy(scales.data.data(), &scale, sizeof(scale));

    const std::array barriers{test::toColorAttachment(color), test::toDepthAttachment(depth)};
    commands.barrier(barriers);
    const ColorAttachment attachment{.image = color, .clearColor = {0.0f, 0.0f, 0.0f, 1.0f}};
    const DepthAttachment depthAttachment{.image = depth};
    commands.beginRendering({.colors = {&attachment, 1}, .depth = &depthAttachment});
    commands.bindPipeline(pipeline);
    const std::array bindings{
        BufferBinding{.binding = PassUniformBinding,
                      .buffer = passConstants.buffer,
                      .offset = passConstants.offset,
                      .size = passConstants.data.size()},
        BufferBinding{.binding = PassStorageBinding,
                      .buffer = scales.buffer,
                      .offset = scales.offset,
                      .size = scales.data.size()},
    };
    commands.bindBuffers(bindings);
    const PushConstants push{device->bufferAddress(vertices), {1.0f, 1.0f, 1.0f, 1.0f}};
    commands.pushConstants(std::as_bytes(std::span{&push, 1}));
    commands.bindIndexBuffer(indices, IndexType::Uint16);
    commands.drawIndexed(3, 1, 0);
    commands.drawIndexed(3, 1, 3);
    commands.endRendering();
    test::transition(commands, test::colorToTransferSrc(color));
    commands.copyImageToBuffer(color, readback);
    if (withTimestamps) {
      commands.writeTimestamp(1);
    }
    device->endFrame();
  }

  Pixel centre() {
    device->waitIdle();
    return pixelAt(device->mappedRange(readback), size, size.x / 2, size.y / 2);
  }
};

} // namespace

TEST_CASE("vertices pulled by device address and per-pass buffers reach the shader", "[rhi][mesh][gpu]") {
  test::TestDevice device;
  MeshScene scene{device};
  REQUIRE(device->bufferAddress(scene.vertices) != 0);
  REQUIRE(device->bufferAddress(scene.indices) == 0); // no Storage usage, no address
  const PipelineHandle pipeline = scene.makePipeline(false);
  scene.drawFrame(pipeline, false);
  const Pixel centre = scene.centre();
  // Drawn last without a depth test: the blue triangle covers the red one.
  REQUIRE(centre.b == 255);
  REQUIRE(centre.r == 0);
  REQUIRE(centre.a == 255);
  device->destroyPipeline(pipeline);
}

TEST_CASE("reversed-Z depth test keeps the nearer fragment", "[rhi][mesh][depth][gpu]") {
  test::TestDevice device;
  MeshScene scene{device};
  const PipelineHandle pipeline = scene.makePipeline(true);
  scene.drawFrame(pipeline, false);
  const Pixel centre = scene.centre();
  REQUIRE(centre.r == 255);
  REQUIRE(centre.b == 0);
  device->destroyPipeline(pipeline);
}

TEST_CASE("timestamps written by a frame are read when its slot is reused", "[rhi][mesh][timestamps][gpu]") {
  test::TestDevice device;
  if (!device->info().timestampsSupported) {
    SKIP("no timestamps on " << device->info().deviceName);
  }
  MeshScene scene{device};
  const PipelineHandle pipeline = scene.makePipeline(true);
  // Frame 0 writes two timestamps; the frames in between write none, so their slots read empty.
  scene.drawFrame(pipeline, true);
  for (std::uint32_t i = 1; i < FramesInFlight; ++i) {
    static_cast<void>(device->beginFrame());
    REQUIRE(device->timestamps().empty());
    device->endFrame();
  }
  static_cast<void>(device->beginFrame());
  const std::span<const std::uint64_t> stamps = device->timestamps();
  REQUIRE(stamps.size() == 2);
  REQUIRE(stamps[0] > 0);
  REQUIRE(stamps[1] >= stamps[0]);
  device->endFrame();
  device->destroyPipeline(pipeline);
}

TEST_CASE("the transient allocator is reset per frame and refuses oversized requests", "[rhi][transient]") {
  test::TestDevice device;
  static_cast<void>(device->beginFrame());
  const TransientAllocation a = device->allocateTransient(100);
  const TransientAllocation b = device->allocateTransient(100);
  REQUIRE(a.buffer == b.buffer);
  REQUIRE(a.data.size() == 100);
  REQUIRE(b.offset >= a.offset + 100);
  REQUIRE(b.offset % 16 == 0);
  REQUIRE(device->allocateTransient(1u << 30).data.empty());
  device->endFrame();
  for (std::uint32_t i = 1; i < FramesInFlight; ++i) {
    static_cast<void>(device->beginFrame());
    device->endFrame();
  }
  static_cast<void>(device->beginFrame());
  const TransientAllocation again = device->allocateTransient(100);
  REQUIRE(again.buffer == a.buffer);
  REQUIRE(again.offset == a.offset);
  device->endFrame();
}
