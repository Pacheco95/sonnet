#include <sonnet/editor/Editor.h>

#include <sonnet/core/Error.h>
#include <sonnet/platform/Event.h>
#include <sonnet/platform/Platform.h>
#include <sonnet/rhi/Device.h>
#include <sonnet/rhi/Swapchain.h>

#include <catch2/catch_test_macros.hpp>

#include <memory>

using namespace sonnet;

TEST_CASE("the editor runs frames headless without validation errors", "[editor][gpu]") {
  platform::Platform platform{{.headless = true}};
  std::unique_ptr<platform::IWindow> window;
  std::unique_ptr<rhi::IDevice> device;
  std::unique_ptr<rhi::ISwapchain> swapchain;
  try {
    window = platform.createWindow({.title = "editor_tests", .size = {800, 600}});
    device = rhi::createDevice({.platform = &platform, .applicationName = "editor_tests"});
  } catch (const core::Exception &e) {
    SKIP("no usable Vulkan 1.4 device: " << e.what());
  }
  const rhi::DeviceInfo &info = device->info();
  if (info.loaderVersion < VK_API_VERSION_1_4 && info.driverName != "llvmpipe") {
    SKIP("headless surfaces are not trusted on this loader and driver");
  }
  try {
    swapchain = device->createSwapchain(*window);
  } catch (const core::Exception &e) {
    SKIP("headless surfaces are not supported here: " << e.what());
  }

  {
    editor::Editor editor{platform, *window, *device, *swapchain};
    for (int frame = 0; frame < 4; ++frame) {
      editor.event(platform::MouseMoved{{10.0f, 10.0f}, {1.0f, 0.0f}});
      editor.update(1.0f / 60.0f);
      rhi::ICommandList &commands = device->beginFrame();
      const auto image = swapchain->acquire();
      REQUIRE(image.has_value());
      editor.render(commands, image);
      device->endFrame();
      editor.afterPresent();
      REQUIRE(!editor.quitRequested());
    }
    // A frame without a swapchain image (minimised window) still records the scene.
    editor.update(1.0f / 60.0f);
    rhi::ICommandList &commands = device->beginFrame();
    editor.render(commands, std::nullopt);
    device->endFrame();
    editor.afterPresent();
  }
  device->waitIdle();
  REQUIRE(device->validationMessageCount() == 0);
}
