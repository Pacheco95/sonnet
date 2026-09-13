#include "TestDevice.h"

#include <sonnet/core/File.h>
#include <sonnet/rhi/Device.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstring>

using namespace sonnet::rhi;

namespace {

std::uint32_t uintAt(std::span<const std::byte> pixels, glm::uvec2 size, unsigned x, unsigned y) {
  std::uint32_t value = 0;
  std::memcpy(&value, pixels.data() + (std::size_t{y} * size.x + x) * 4, sizeof(value));
  return value;
}

int redAt(std::span<const std::byte> pixels, glm::uvec2 size, unsigned x, unsigned y) {
  return std::to_integer<int>(pixels[(std::size_t{y} * size.x + x) * 4]);
}

ShaderHandle loadShader(test::TestDevice &device, const char *name) {
  const auto spirv = sonnet::core::readFile(device.platform.basePath() / "shaders" / (std::string{name} + ".spv"));
  REQUIRE(spirv.has_value());
  return device->createShader({.spirv = *spirv, .debugName = name});
}

} // namespace

TEST_CASE("an R32Uint attachment holds ids that a later pass reads through the pass image binding", "[rhi][id][gpu]") {
  test::TestDevice device;
  constexpr glm::uvec2 size{32, 32};
  constexpr std::uint32_t TriangleId = 0xABCDEF01u;

  const ShaderHandle idShader = loadShader(device, "id");
  const ShaderHandle readShader = loadShader(device, "readid");
  const PipelineHandle idPipeline = device->createGraphicsPipeline(
      {.shader = idShader, .colorFormats = {Format::R32Uint}, .cullMode = CullMode::None, .debugName = "id"});
  const PipelineHandle readPipeline = device->createGraphicsPipeline({.shader = readShader,
                                                                      .colorFormats = {Format::R8G8B8A8Unorm},
                                                                      .cullMode = CullMode::None,
                                                                      .debugName = "read id"});
  const ImageHandle ids =
      device->createImage({.size = size,
                           .format = Format::R32Uint,
                           .usage = ImageUsage::ColorAttachment | ImageUsage::Sampled | ImageUsage::TransferSrc,
                           .debugName = "ids"});
  const ImageHandle color = device->createImage({.size = size,
                                                 .format = Format::R8G8B8A8Unorm,
                                                 .usage = ImageUsage::ColorAttachment | ImageUsage::TransferSrc,
                                                 .debugName = "color"});
  const std::uint64_t bytes = std::uint64_t{size.x} * size.y * 4;
  const BufferHandle idReadback = device->createBuffer(
      {.size = bytes, .usage = BufferUsage::TransferDst, .memory = MemoryUsage::GpuToCpu, .debugName = "id readback"});
  const BufferHandle colorReadback = device->createBuffer({.size = bytes,
                                                           .usage = BufferUsage::TransferDst,
                                                           .memory = MemoryUsage::GpuToCpu,
                                                           .debugName = "color readback"});

  ICommandList &commands = device->beginFrame();
  {
    const std::array barriers{test::toColorAttachment(ids)};
    commands.barrier(barriers);
    // The clear value 7 is an integer for this format, not a normalised colour.
    const ColorAttachment attachment{.image = ids, .clearColor = {7.0f, 0.0f, 0.0f, 0.0f}};
    commands.beginRendering({.colors = {&attachment, 1}});
    commands.bindPipeline(idPipeline);
    commands.pushConstants(std::as_bytes(std::span{&TriangleId, 1}));
    commands.draw(3);
    commands.endRendering();
  }
  {
    const std::array barriers{
        ImageBarrier{.image = ids,
                     .srcStage = PipelineStage::ColorAttachmentOutput,
                     .srcAccess = Access::ColorAttachmentWrite,
                     .oldLayout = ImageLayout::ColorAttachment,
                     .dstStage = PipelineStage::FragmentShader,
                     .dstAccess = Access::ShaderRead,
                     .newLayout = ImageLayout::ShaderReadOnly},
        test::toColorAttachment(color),
    };
    commands.barrier(barriers);
    const ColorAttachment attachment{.image = color, .clearColor = {0.0f, 0.0f, 0.0f, 1.0f}};
    commands.beginRendering({.colors = {&attachment, 1}});
    commands.bindPipeline(readPipeline);
    const ImageBinding binding{.binding = PassImageBinding, .image = ids};
    commands.bindImages({&binding, 1});
    commands.pushConstants(std::as_bytes(std::span{&TriangleId, 1}));
    commands.draw(3);
    commands.endRendering();
  }
  {
    const std::array barriers{
        ImageBarrier{.image = ids,
                     .srcStage = PipelineStage::FragmentShader,
                     .srcAccess = Access::ShaderRead,
                     .oldLayout = ImageLayout::ShaderReadOnly,
                     .dstStage = PipelineStage::Transfer,
                     .dstAccess = Access::TransferRead,
                     .newLayout = ImageLayout::TransferSrc},
        test::colorToTransferSrc(color),
    };
    commands.barrier(barriers);
    commands.copyImageToBuffer(ids, idReadback);
    commands.copyImageToBuffer(color, colorReadback);
  }
  device->endFrame();
  device->waitIdle();

  const std::span<const std::byte> idPixels = device->mappedRange(idReadback);
  REQUIRE(uintAt(idPixels, size, size.x / 2, size.y / 2) == TriangleId);
  REQUIRE(uintAt(idPixels, size, 0, 0) == 7);
  const std::span<const std::byte> colorPixels = device->mappedRange(colorReadback);
  REQUIRE(redAt(colorPixels, size, size.x / 2, size.y / 2) == 255);
  REQUIRE(redAt(colorPixels, size, 0, 0) == 0);

  device->destroyBuffer(colorReadback);
  device->destroyBuffer(idReadback);
  device->destroyImage(color);
  device->destroyImage(ids);
  device->destroyPipeline(readPipeline);
  device->destroyPipeline(idPipeline);
  device->destroyShader(readShader);
  device->destroyShader(idShader);
}
