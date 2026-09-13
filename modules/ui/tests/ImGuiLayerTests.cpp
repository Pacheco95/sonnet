#include <sonnet/ui/ImGuiLayer.h>

#include <sonnet/core/Error.h>
#include <sonnet/platform/Platform.h>
#include <sonnet/rhi/Device.h>
#include <sonnet/rhi/NullDevice.h>
#include <sonnet/rhi/Swapchain.h>

#include <catch2/catch_test_macros.hpp>

#include <imgui.h>

#include <array>
#include <memory>

using namespace sonnet;

namespace {

// A headless window, a device and a swapchain, or a skip where the machine cannot provide them.
struct Fixture {
  platform::Platform platform{{.headless = true}};
  std::unique_ptr<platform::IWindow> window;
  std::unique_ptr<rhi::IDevice> device;
  std::unique_ptr<rhi::ISwapchain> swapchain;

  Fixture() {
    try {
      window = platform.createWindow({.title = "ui_tests", .size = {320, 240}});
      device = rhi::createDevice({.platform = &platform, .applicationName = "ui_tests"});
    } catch (const core::Exception &e) {
      SKIP("no usable Vulkan 1.4 device: " << e.what());
    }
    // Headless surfaces are only trusted on Lavapipe or a 1.4 loader (see rhi's swapchain tests).
    const rhi::DeviceInfo &info = device->info();
    if (info.loaderVersion < VK_API_VERSION_1_4 && info.driverName != "llvmpipe") {
      SKIP("headless surfaces are not trusted on this loader and driver");
    }
    try {
      swapchain = device->createSwapchain(*window);
    } catch (const core::Exception &e) {
      SKIP("headless surfaces are not supported here: " << e.what());
    }
  }

  ~Fixture() {
    if (device) {
      CHECK(device->validationMessageCount() == 0);
    }
  }

  ui::ImGuiLayerDesc layerDesc() {
    return {.window = window.get(),
            .device = device.get(),
            .swapchainFormat = swapchain->format(),
            .swapchainImageCount = swapchain->imageCount(),
            .docking = true,
            .viewports = false};
  }
};

rhi::ImageBarrier toColor(rhi::ImageHandle image) {
  return {.image = image,
          .srcStage = rhi::PipelineStage::AllCommands,
          .oldLayout = rhi::ImageLayout::Undefined,
          .dstStage = rhi::PipelineStage::ColorAttachmentOutput,
          .dstAccess = rhi::Access::ColorAttachmentWrite,
          .newLayout = rhi::ImageLayout::ColorAttachment};
}

rhi::ImageBarrier toPresent(rhi::ImageHandle image) {
  return {.image = image,
          .srcStage = rhi::PipelineStage::ColorAttachmentOutput,
          .srcAccess = rhi::Access::ColorAttachmentWrite,
          .oldLayout = rhi::ImageLayout::ColorAttachment,
          .dstStage = rhi::PipelineStage::None,
          .newLayout = rhi::ImageLayout::Present};
}

} // namespace

TEST_CASE("the layer refuses a device that is not Vulkan", "[ui]") {
  platform::Platform platform{{.headless = true}};
  std::unique_ptr<platform::IWindow> window;
  try {
    window = platform.createWindow({.title = "ui_tests"});
  } catch (const core::Exception &e) {
    SKIP("no window on this machine: " << e.what());
  }
  const auto device = rhi::createNullDevice();
  REQUIRE_THROWS_AS(ui::ImGuiLayer({.window = window.get(),
                                    .device = device.get(),
                                    .swapchainFormat = rhi::Format::B8G8R8A8Unorm,
                                    .swapchainImageCount = 3}),
                    core::Exception);
}

TEST_CASE("frames with a window and an image draw into the swapchain without validation errors", "[ui][gpu]") {
  Fixture fixture;
  ui::ImGuiLayer layer{fixture.layerDesc()};
  REQUIRE(ImGui::GetCurrentContext() != nullptr);
  REQUIRE((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable) != 0);

  const rhi::ImageHandle picture =
      fixture.device->createImage({.size = {16, 16},
                                   .format = rhi::Format::R8G8B8A8Unorm,
                                   .usage = rhi::ImageUsage::Sampled | rhi::ImageUsage::TransferDst,
                                   .debugName = "picture"});
  const ImTextureID texture = layer.registerImage(picture);
  REQUIRE(texture != 0);

  for (int frame = 0; frame < 4; ++frame) {
    layer.beginFrame();
    ImGui::Begin("panel");
    ImGui::Text("frame %d", frame);
    if (frame < 2) {
      ImGui::Image(texture, ImVec2{16.0f, 16.0f});
    }
    ImGui::End();
    layer.endFrame();
    REQUIRE(ImGui::GetDrawData() != nullptr);

    rhi::ICommandList &commands = fixture.device->beginFrame();
    if (frame == 0) {
      // The picture is never written; a transition from Undefined gives it a defined layout.
      const rhi::ImageBarrier barrier{.image = picture,
                                      .srcStage = rhi::PipelineStage::AllCommands,
                                      .oldLayout = rhi::ImageLayout::Undefined,
                                      .dstStage = rhi::PipelineStage::FragmentShader,
                                      .dstAccess = rhi::Access::ShaderRead,
                                      .newLayout = rhi::ImageLayout::ShaderReadOnly};
      commands.barrier({&barrier, 1});
    }
    const auto image = fixture.swapchain->acquire();
    REQUIRE(image.has_value());
    const rhi::ImageBarrier begin = toColor(image->image);
    commands.barrier({&begin, 1});
    const rhi::ColorAttachment attachment{.image = image->image, .clearColor = {0.1f, 0.1f, 0.1f, 1.0f}};
    commands.beginRendering({.colors = {&attachment, 1}});
    layer.draw(commands);
    commands.endRendering();
    const rhi::ImageBarrier end = toPresent(image->image);
    commands.barrier({&end, 1});
    fixture.device->endFrame();
    layer.renderPlatformWindows();
    if (frame == 1) {
      layer.unregisterImage(texture); // still referenced by the frame in flight: released later
    }
  }
  fixture.device->waitIdle();
  fixture.device->destroyImage(picture);
}
