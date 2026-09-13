#include "TestDevice.h"

#include <sonnet/rhi/Device.h>
#include <sonnet/rhi/Swapchain.h>

#include <catch2/catch_test_macros.hpp>

using namespace sonnet::rhi;

namespace {

std::unique_ptr<ISwapchain> makeSwapchain(test::TestDevice &device, sonnet::platform::IWindow &window) {
  if (!device.headlessSurfacesUsable()) {
    SKIP("headless surfaces are not trusted on loader " << device->info().loaderVersion << " with driver "
                                                        << device->info().driverName);
  }
  try {
    return device->createSwapchain(window);
  } catch (const sonnet::core::Exception &e) {
    SKIP("headless surfaces are not supported here: " << e.what());
  }
}

} // namespace

TEST_CASE("swapchain is created from a headless window", "[rhi][swapchain][gpu]") {
  test::TestDevice device;
  const auto window = device.platform.createWindow({.title = "swapchain", .size = {320, 200}});
  const auto swapchain = makeSwapchain(device, *window);
  REQUIRE(swapchain->extent() == glm::uvec2{320, 200});
  REQUIRE(swapchain->format() != Format::Undefined);
  REQUIRE(swapchain->imageCount() >= 2);
}

TEST_CASE("acquired images are cleared and presented over several frames", "[rhi][swapchain][gpu]") {
  test::TestDevice device;
  const auto window = device.platform.createWindow({.title = "present", .size = {320, 200}});
  const auto swapchain = makeSwapchain(device, *window);

  for (int frame = 0; frame < 6; ++frame) {
    ICommandList &commands = device->beginFrame();
    const std::optional<SwapchainImage> image = swapchain->acquire();
    REQUIRE(image.has_value());
    REQUIRE(device->isValid(image->image));
    REQUIRE(image->extent == swapchain->extent());
    test::transition(commands, test::toColorAttachment(image->image));
    const ColorAttachment attachment{.image = image->image, .clearColor = {0.1f, 0.2f, 0.3f, 1.0f}};
    commands.beginRendering({.colors = {&attachment, 1}});
    commands.endRendering();
    test::transition(commands, test::toPresent(image->image, ImageLayout::ColorAttachment));
    device->endFrame();
  }
  device->waitIdle();
}

TEST_CASE("a resize request recreates the swapchain at the next acquire", "[rhi][swapchain][gpu]") {
  test::TestDevice device;
  const auto window = device.platform.createWindow({.title = "resize", .size = {320, 200}});
  const auto swapchain = makeSwapchain(device, *window);
  const std::uint32_t before = swapchain->imageCount();

  swapchain->requestResize();
  ICommandList &commands = device->beginFrame();
  const std::optional<SwapchainImage> image = swapchain->acquire();
  REQUIRE(image.has_value());
  REQUIRE(swapchain->imageCount() == before);
  test::transition(commands, test::toPresent(image->image, ImageLayout::Undefined));
  device->endFrame();
  device->waitIdle();
}
