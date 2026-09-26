#pragma once

#include <sonnet/rhi/Device.h>

#include <sonnet/core/Error.h>
#include <sonnet/platform/Platform.h>

#include <catch2/catch_test_macros.hpp>

#include <vulkan/vulkan_core.h>

#include <cstdlib>
#include <memory>
#include <string_view>

namespace sonnet::rhi::test {

// The cap every test device takes, from SONNET_TEST_API_VERSION_CAP ("1.3"), which CTest's
// rhi_tests_vulkan_1_3 sets to run the suite on ADR-0019's 1.3 path; 0 when unset.
inline std::uint32_t apiVersionCap() {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996) // getenv is the standard call; _dupenv_s is MSVC-only
#endif
  const char *cap = std::getenv("SONNET_TEST_API_VERSION_CAP");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
  if (cap == nullptr || !std::string_view{cap}.starts_with("1.")) {
    return 0;
  }
  return VK_MAKE_API_VERSION(0, 1, static_cast<std::uint32_t>(std::atoi(cap + 2)), 0);
}

// A headless platform and a device. Skips the test when no suitable Vulkan implementation is
// reachable, which is the case on CI runners without Lavapipe.
struct TestDevice {
  platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device;

  explicit TestDevice(std::uint32_t versionCap = apiVersionCap()) {
    try {
      device = createDevice({.platform = &platform, .applicationName = "rhi_tests", .apiVersionCap = versionCap});
    } catch (const core::Exception &e) {
      SKIP("no usable Vulkan device: " << e.what());
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
