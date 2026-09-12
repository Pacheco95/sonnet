#include "TestDevice.h"

#include <sonnet/core/File.h>
#include <sonnet/rhi/Device.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace sonnet::rhi;

namespace {

struct Pixel {
  int r, g, b, a;
};

Pixel pixelAt(std::span<const std::byte> pixels, glm::uvec2 size, unsigned x, unsigned y) {
  const std::size_t offset = (std::size_t{y} * size.x + x) * 4;
  return {std::to_integer<int>(pixels[offset]), std::to_integer<int>(pixels[offset + 1]),
          std::to_integer<int>(pixels[offset + 2]), std::to_integer<int>(pixels[offset + 3])};
}

std::vector<std::byte> loadTriangleShader(test::TestDevice &device) {
  const auto bytes = sonnet::core::readFile(device.platform.basePath() / "shaders" / "triangle.spv");
  REQUIRE(bytes.has_value());
  return *bytes;
}

} // namespace

TEST_CASE("shaders and pipelines are created and destroyed with generation checks", "[rhi][pipeline]") {
  test::TestDevice device;
  const std::vector<std::byte> spirv = loadTriangleShader(device);
  const ShaderHandle shader = device->createShader({.spirv = spirv, .debugName = "triangle"});
  REQUIRE(device->isValid(shader));
  const PipelineHandle pipeline = device->createGraphicsPipeline(
      {.shader = shader, .colorFormats = {Format::R8G8B8A8Unorm}, .debugName = "triangle pipeline"});
  REQUIRE(device->isValid(pipeline));

  device->destroyShader(shader);
  REQUIRE(!device->isValid(shader));
  REQUIRE(device->isValid(pipeline)); // pipelines do not depend on their module after creation
  device->destroyPipeline(pipeline);
  REQUIRE(!device->isValid(pipeline));
  device->destroyPipeline(pipeline); // stale: logged and ignored
}

TEST_CASE("invalid SPIR-V is rejected at shader creation", "[rhi][pipeline]") {
  test::TestDevice device;
  const std::array<std::byte, 8> garbage{};
  REQUIRE_THROWS_AS(device->createShader({.spirv = garbage, .debugName = "garbage"}), sonnet::core::Exception);
  const std::array<std::byte, 3> odd{};
  REQUIRE_THROWS_AS(device->createShader({.spirv = odd, .debugName = "odd"}), sonnet::core::Exception);
}

TEST_CASE("a triangle drawn through the rhi covers the centre and not the corners", "[rhi][pipeline][gpu]") {
  test::TestDevice device;
  constexpr glm::uvec2 size{64, 64};
  constexpr std::size_t byteCount = std::size_t{size.x} * size.y * 4;
  const std::vector<std::byte> spirv = loadTriangleShader(device);
  const ShaderHandle shader = device->createShader({.spirv = spirv, .debugName = "triangle"});
  const PipelineHandle pipeline = device->createGraphicsPipeline(
      {.shader = shader, .colorFormats = {Format::R8G8B8A8Unorm}, .debugName = "triangle pipeline"});
  device->destroyShader(shader);
  const ImageHandle target = device->createImage({.size = size,
                                                  .format = Format::R8G8B8A8Unorm,
                                                  .usage = ImageUsage::ColorAttachment | ImageUsage::TransferSrc,
                                                  .debugName = "triangle target"});
  const BufferHandle readback = device->createBuffer(
      {.size = byteCount, .usage = BufferUsage::TransferDst, .memory = MemoryUsage::GpuToCpu, .debugName = "readback"});

  ICommandList &commands = device->beginFrame();
  commands.barrier(target, ImageLayout::Undefined, ImageLayout::ColorAttachment);
  const ColorAttachment attachment{.image = target, .clearColor = {0.0f, 0.0f, 0.0f, 1.0f}};
  commands.beginRendering({&attachment, 1});
  commands.bindPipeline(pipeline);
  const glm::vec4 tint{1.0f, 1.0f, 1.0f, 1.0f};
  commands.pushConstants(std::as_bytes(std::span{&tint, 1}));
  commands.draw(3);
  commands.endRendering();
  commands.barrier(target, ImageLayout::ColorAttachment, ImageLayout::TransferSrc);
  commands.copyImageToBuffer(target, readback);
  device->endFrame();
  device->waitIdle();

  const std::span<const std::byte> pixels = device->mappedRange(readback);
  REQUIRE(pixels.size() == byteCount);
  // The triangle spans y in [-0.5, 0.5] with its apex at the top; with the Y flip the apex is
  // the smaller row index. The centre pixel is inside; the four corners are outside.
  const Pixel centre = pixelAt(pixels, size, size.x / 2, size.y / 2);
  REQUIRE(centre.a == 255);
  REQUIRE(centre.r + centre.g + centre.b > 200);
  constexpr std::array<std::pair<unsigned, unsigned>, 4> corners{{{0, 0}, {63, 0}, {0, 63}, {63, 63}}};
  for (const auto &[x, y] : corners) {
    const Pixel corner = pixelAt(pixels, size, x, y);
    REQUIRE(corner.r == 0);
    REQUIRE(corner.g == 0);
    REQUIRE(corner.b == 0);
    REQUIRE(corner.a == 255);
  }
  // Apex is red (vertex 0) and near the top row; the bottom row is outside the triangle.
  const Pixel nearApex = pixelAt(pixels, size, size.x / 2, size.y / 4 + 1);
  REQUIRE(nearApex.r > nearApex.g);
  REQUIRE(nearApex.r > nearApex.b);
  const Pixel bottom = pixelAt(pixels, size, size.x / 2, size.y - 1);
  REQUIRE(bottom.r + bottom.g + bottom.b == 0);

  device->destroyBuffer(readback);
  device->destroyImage(target);
  device->destroyPipeline(pipeline);
}

TEST_CASE("the tint push constant scales the triangle colour", "[rhi][pipeline][gpu]") {
  test::TestDevice device;
  constexpr glm::uvec2 size{16, 16};
  constexpr std::size_t byteCount = std::size_t{size.x} * size.y * 4;
  const std::vector<std::byte> spirv = loadTriangleShader(device);
  const ShaderHandle shader = device->createShader({.spirv = spirv, .debugName = "triangle"});
  const PipelineHandle pipeline = device->createGraphicsPipeline(
      {.shader = shader, .colorFormats = {Format::R8G8B8A8Unorm}, .debugName = "triangle pipeline"});
  const ImageHandle target = device->createImage({.size = size,
                                                  .format = Format::R8G8B8A8Unorm,
                                                  .usage = ImageUsage::ColorAttachment | ImageUsage::TransferSrc,
                                                  .debugName = "tint target"});
  const BufferHandle readback = device->createBuffer(
      {.size = byteCount, .usage = BufferUsage::TransferDst, .memory = MemoryUsage::GpuToCpu, .debugName = "readback"});

  ICommandList &commands = device->beginFrame();
  commands.barrier(target, ImageLayout::Undefined, ImageLayout::ColorAttachment);
  const ColorAttachment attachment{.image = target, .clearColor = {0.0f, 0.0f, 0.0f, 1.0f}};
  commands.beginRendering({&attachment, 1});
  commands.bindPipeline(pipeline);
  const glm::vec4 tint{0.0f, 0.0f, 0.0f, 1.0f};
  commands.pushConstants(std::as_bytes(std::span{&tint, 1}));
  commands.draw(3);
  commands.endRendering();
  commands.barrier(target, ImageLayout::ColorAttachment, ImageLayout::TransferSrc);
  commands.copyImageToBuffer(target, readback);
  device->endFrame();
  device->waitIdle();

  const Pixel centre = pixelAt(device->mappedRange(readback), size, size.x / 2, size.y / 2);
  REQUIRE(centre.r + centre.g + centre.b == 0);
  REQUIRE(centre.a == 255);

  device->destroyBuffer(readback);
  device->destroyImage(target);
  device->destroyPipeline(pipeline);
  device->destroyShader(shader);
}
