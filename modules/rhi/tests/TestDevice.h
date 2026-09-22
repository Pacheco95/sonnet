#pragma once

#include <sonnet/rhi/Device.h>

#include <sonnet/core/Error.h>
#include <sonnet/platform/Platform.h>

#include <catch2/catch_test_macros.hpp>

#include <vulkan/vulkan_core.h>

#include <memory>

namespace sonnet::rhi::test {

// A headless platform and a device. Skips the test when no Vulkan 1.4 implementation is
// reachable, which is the case on CI runners without Lavapipe.
struct TestDevice {
  platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device;

  // `disableDrawIndirectCount` runs the device as MoltenVK does, without the count form of the
  // indirect draws (ADR-0014).
  explicit TestDevice(bool disableDrawIndirectCount = false) {
    try {
      device = createDevice({.platform = &platform,
                             .applicationName = "rhi_tests",
                             .disableDrawIndirectCount = disableDrawIndirectCount});
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

  // Loaders before 1.4 emulate VK_EXT_headless_surface and advertise it for every driver; a
  // driver without the extension can then crash inside the surface queries (NVIDIA does). Only
  // Lavapipe is trusted on such loaders; a 1.4 loader fails cleanly where unsupported.
  [[nodiscard]] bool headlessSurfacesUsable() const {
    const DeviceInfo &info = device->info();
    return info.loaderVersion >= VK_API_VERSION_1_4 || info.driverName == "llvmpipe";
  }

  IDevice &operator*() {
    return *device;
  }
  IDevice *operator->() {
    return device.get();
  }
};

// The transitions the tests spell out; the render graph derives the same ones from pass usage.
inline ImageBarrier toColorAttachment(ImageHandle image) {
  return {.image = image,
          .srcStage = PipelineStage::AllCommands,
          .srcAccess = Access::None,
          .oldLayout = ImageLayout::Undefined,
          .dstStage = PipelineStage::ColorAttachmentOutput,
          .dstAccess = Access::ColorAttachmentWrite,
          .newLayout = ImageLayout::ColorAttachment};
}
inline ImageBarrier toDepthAttachment(ImageHandle image) {
  return {.image = image,
          .srcStage = PipelineStage::AllCommands,
          .srcAccess = Access::None,
          .oldLayout = ImageLayout::Undefined,
          .dstStage = PipelineStage::EarlyFragmentTests | PipelineStage::LateFragmentTests,
          .dstAccess = Access::DepthAttachmentRead | Access::DepthAttachmentWrite,
          .newLayout = ImageLayout::DepthAttachment};
}
inline ImageBarrier colorToTransferSrc(ImageHandle image) {
  return {.image = image,
          .srcStage = PipelineStage::ColorAttachmentOutput,
          .srcAccess = Access::ColorAttachmentWrite,
          .oldLayout = ImageLayout::ColorAttachment,
          .dstStage = PipelineStage::Transfer,
          .dstAccess = Access::TransferRead,
          .newLayout = ImageLayout::TransferSrc};
}
inline ImageBarrier toPresent(ImageHandle image, ImageLayout from) {
  return {.image = image,
          .srcStage =
              from == ImageLayout::Undefined ? PipelineStage::AllCommands : PipelineStage::ColorAttachmentOutput,
          .srcAccess = from == ImageLayout::Undefined ? Access::None : Access::ColorAttachmentWrite,
          .oldLayout = from,
          .dstStage = PipelineStage::AllCommands,
          .dstAccess = Access::None,
          .newLayout = ImageLayout::Present};
}
inline void transition(ICommandList &commands, const ImageBarrier &barrier) {
  commands.barrier({&barrier, 1});
}

} // namespace sonnet::rhi::test
