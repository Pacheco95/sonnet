#pragma once

#include <sonnet/rhi/Device.h>

#include <sonnet/core/Error.h>
#include <sonnet/platform/Platform.h>

#include <catch2/catch_test_macros.hpp>

#include <memory>

namespace sonnet::rhi::test {

// A headless platform and a device. Skips the test when no Vulkan 1.4 implementation is
// reachable, which is the case on CI runners without Lavapipe.
struct TestDevice {
  platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device;

  TestDevice() {
    try {
      device = createDevice({.platform = &platform, .applicationName = "rhi_tests"});
    } catch (const core::Exception &e) {
      SKIP("no usable Vulkan 1.4 device: " << e.what());
    }
  }

  ~TestDevice() {
    // Validation output is a test failure, not a log line to scroll past.
    if (device) {
      CHECK(device->validationMessageCount() == 0);
    }
  }

  TestDevice(const TestDevice &) = delete;
  TestDevice &operator=(const TestDevice &) = delete;

  IDevice &operator*() {
    return *device;
  }
  IDevice *operator->() {
    return device.get();
  }
};

} // namespace sonnet::rhi::test
