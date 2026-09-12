#include "TestDevice.h"

#include <sonnet/rhi/Device.h>

#include <catch2/catch_test_macros.hpp>

#include <vulkan/vulkan_core.h>

using namespace sonnet::rhi;

TEST_CASE("device reports a Vulkan 1.4 implementation", "[rhi][device]") {
  test::TestDevice device;
  const DeviceInfo &info = device->info();
  REQUIRE(!info.deviceName.empty());
  REQUIRE(VK_API_VERSION_MAJOR(info.apiVersion) == 1);
  REQUIRE(VK_API_VERSION_MINOR(info.apiVersion) >= 4);
  REQUIRE(!info.driverName.empty());
  REQUIRE(info.loaderVersion >= VK_API_VERSION_1_1);
}

TEST_CASE("frames can be begun and ended without work", "[rhi][device]") {
  test::TestDevice device;
  for (int i = 0; i < 2 * static_cast<int>(FramesInFlight) + 1; ++i) {
    ICommandList &commands = device->beginFrame();
    static_cast<void>(commands);
    device->endFrame();
  }
  device->waitIdle();
}

TEST_CASE("two devices can coexist in one process", "[rhi][device]") {
  test::TestDevice first;
  sonnet::platform::Platform &platform = first.platform;
  const auto second = createDevice({.platform = &platform, .applicationName = "rhi_tests_second"});
  REQUIRE(second->info().deviceName == first->info().deviceName);
}
