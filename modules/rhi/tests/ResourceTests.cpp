#include "TestDevice.h"

#include <sonnet/rhi/Device.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdlib>
#include <cstring>

using namespace sonnet::rhi;

TEST_CASE("buffers are created, mapped and destroyed", "[rhi][resources]") {
  test::TestDevice device;
  const BufferHandle upload = device->createBuffer(
      {.size = 256, .usage = BufferUsage::TransferSrc, .memory = MemoryUsage::CpuToGpu, .debugName = "upload"});
  const BufferHandle gpu = device->createBuffer(
      {.size = 1024, .usage = BufferUsage::Storage | BufferUsage::TransferDst, .debugName = "gpu only"});
  REQUIRE(device->isValid(upload));
  REQUIRE(device->isValid(gpu));
  REQUIRE(device->mappedRange(upload).size() == 256);
  REQUIRE(device->mappedRange(gpu).empty());

  std::array<std::byte, 4> pattern{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
  std::memcpy(device->mappedRange(upload).data(), pattern.data(), pattern.size());
  REQUIRE(std::memcmp(device->mappedRange(upload).data(), pattern.data(), pattern.size()) == 0);

  device->destroyBuffer(upload);
  REQUIRE(!device->isValid(upload));
  REQUIRE(device->mappedRange(upload).empty());
  device->destroyBuffer(gpu);
  REQUIRE(!device->isValid(gpu));
}

TEST_CASE("a destroyed handle never resolves to the slot's next resource", "[rhi][resources]") {
  test::TestDevice device;
  const BufferHandle first = device->createBuffer({.size = 16, .usage = BufferUsage::Uniform, .debugName = "first"});
  device->destroyBuffer(first);
  const BufferHandle second = device->createBuffer({.size = 16, .usage = BufferUsage::Uniform, .debugName = "second"});
  REQUIRE(second.index == first.index);
  REQUIRE(!device->isValid(first));
  REQUIRE(device->isValid(second));
  device->destroyBuffer(first); // stale: logged and ignored
  REQUIRE(device->isValid(second));
  device->destroyBuffer(second);
}

TEST_CASE("images are created and destroyed, deferred across frames", "[rhi][resources]") {
  test::TestDevice device;
  const ImageHandle image = device->createImage(
      {.size = {64, 32}, .format = Format::R8G8B8A8Unorm, .usage = ImageUsage::ColorAttachment, .debugName = "target"});
  REQUIRE(device->isValid(image));
  REQUIRE(device->imageDesc(image).size == glm::uvec2{64, 32});

  ICommandList &commands = device->beginFrame();
  test::transition(commands, test::toColorAttachment(image));
  device->destroyImage(image); // still referenced by this frame's commands: released later
  REQUIRE(!device->isValid(image));
  device->endFrame();
  for (std::uint32_t i = 0; i < FramesInFlight; ++i) {
    static_cast<void>(device->beginFrame());
    device->endFrame();
  }
  device->waitIdle();
}

TEST_CASE("clearing an image and reading it back yields the clear colour", "[rhi][resources][gpu]") {
  test::TestDevice device;
  constexpr glm::uvec2 size{8, 4};
  constexpr std::size_t byteCount = std::size_t{size.x} * size.y * 4;
  const ImageHandle image = device->createImage({.size = size,
                                                 .format = Format::R8G8B8A8Unorm,
                                                 .usage = ImageUsage::ColorAttachment | ImageUsage::TransferSrc,
                                                 .debugName = "clear target"});
  const BufferHandle readback = device->createBuffer(
      {.size = byteCount, .usage = BufferUsage::TransferDst, .memory = MemoryUsage::GpuToCpu, .debugName = "readback"});

  ICommandList &commands = device->beginFrame();
  test::transition(commands, test::toColorAttachment(image));
  const ColorAttachment attachment{.image = image, .clearColor = {1.0f, 0.2f, 0.0f, 1.0f}};
  commands.beginRendering({.colors = {&attachment, 1}});
  commands.endRendering();
  test::transition(commands, test::colorToTransferSrc(image));
  commands.copyImageToBuffer(image, readback);
  device->endFrame();
  device->waitIdle();

  const std::span<std::byte> pixels = device->mappedRange(readback);
  REQUIRE(pixels.size() == byteCount);
  // 0.2 * 255 = 51; one unit of tolerance for implementations that round differently.
  for (std::size_t i = 0; i < pixels.size(); i += 4) {
    REQUIRE(std::to_integer<int>(pixels[i]) == 255);
    REQUIRE(std::abs(std::to_integer<int>(pixels[i + 1]) - 51) <= 1);
    REQUIRE(std::to_integer<int>(pixels[i + 2]) == 0);
    REQUIRE(std::to_integer<int>(pixels[i + 3]) == 255);
  }

  device->destroyBuffer(readback);
  device->destroyImage(image);
}

TEST_CASE("a resource released between frames outlives the frame that was just submitted", "[rhi][resources][gpu]") {
  test::TestDevice device;
  const ImageHandle image = device->createImage(
      {.size = {256, 256}, .format = Format::R8G8B8A8Unorm, .usage = ImageUsage::ColorAttachment, .debugName = "late"});
  ICommandList &commands = device->beginFrame();
  test::transition(commands, test::toColorAttachment(image));
  const ColorAttachment attachment{.image = image};
  commands.beginRendering({.colors = {&attachment, 1}});
  commands.endRendering();
  device->endFrame();
  // Released after the submit, while the frame that draws it may still be running: it must be
  // freed only once that frame's slot is reused, not at the next beginFrame of the other slot.
  device->destroyImage(image);
  REQUIRE(!device->isValid(image));
  for (std::uint32_t i = 0; i < FramesInFlight + 1; ++i) {
    static_cast<void>(device->beginFrame());
    device->endFrame();
  }
  device->waitIdle();
  // The fixture checks that validation reported no use-after-destroy.
}
