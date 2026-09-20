#include "TestDevice.h"

#include <sonnet/rhi/Device.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>

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

TEST_CASE("a buffer uploaded but never submitted is destroyed without a complaint", "[rhi][device]") {
  test::TestDevice device;
  // What loading a project and dropping it without drawing a frame does: the upload is recorded
  // into the slot's command buffer, which no endFrame ever submits. The slot's next reuse has to
  // put that command buffer back before it runs the destruction it deferred, since a command
  // buffer still in the recording state counts as using what it names.
  const BufferHandle buffer = device->createBuffer(
      {.size = 256, .usage = BufferUsage::Storage | BufferUsage::TransferDst, .debugName = "never submitted"});
  const std::array<std::byte, 256> data{};
  device->uploadBuffer(buffer, 0, data);
  device->waitIdle();
  device->destroyBuffer(buffer);

  for (int i = 0; i < 2 * static_cast<int>(FramesInFlight) + 1; ++i) {
    static_cast<void>(device->beginFrame());
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

TEST_CASE("memory budget lists the device heaps", "[rhi][device]") {
  test::TestDevice device;
  const MemoryBudget budget = device->memoryBudget();
  REQUIRE(budget.heapCount > 0);
  REQUIRE(budget.heapCount <= MemoryBudget::MaxHeaps);
  bool deviceLocal = false;
  for (std::uint32_t i = 0; i < budget.heapCount; ++i) {
    REQUIRE(budget.heaps[i].budget > 0);
    deviceLocal = deviceLocal || budget.heaps[i].deviceLocal;
  }
  REQUIRE(deviceLocal);
}
