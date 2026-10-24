#include <sonnet/ui/ImGuiLayer.h>

#include <sonnet/core/Error.h>
#include <sonnet/platform/Platform.h>
#include <sonnet/rhi/Device.h>
#include <sonnet/rhi/NullDevice.h>
#include <sonnet/rhi/Swapchain.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <SDL3/SDL_video.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <limits>
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

namespace {

// The style fields whose scaled value a test compares. They are the ones `ScaleAllSizes` truncates, so they show a
// derivation that compounds as well as one that is missing.
struct SizesSeen {
  float windowPaddingX;
  float framePaddingY;
  float itemSpacingX;
  float indentSpacing;
  float scrollbarSize;
  float fontScaleDpi;
  float fontScaleMain;
  float fontSize;

  bool operator==(const SizesSeen &) const = default;
};

SizesSeen seen(const ImGuiStyle &style) {
  return {style.WindowPadding.x, style.FramePadding.y, style.ItemSpacing.x, style.IndentSpacing,
          style.ScrollbarSize,   style.FontScaleDpi,   style.FontScaleMain, ImGui::GetFontSize()};
}

// One frame with a floating window of a fixed size, returning what the style looked like inside it.
SizesSeen frame(ui::ImGuiLayer &layer) {
  layer.beginFrame();
  ImGui::SetNextWindowPos(ImVec2{20.0f, 20.0f}, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2{200.0f, 100.0f}, ImGuiCond_FirstUseEver);
  ImGui::Begin("panel", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking);
  const SizesSeen result = seen(ImGui::GetStyle());
  ImGui::End();
  layer.endFrame();
  return result;
}

} // namespace

TEST_CASE("a change of the display scale derives the style and the font scale from the theme again",
          "[ui][gpu][scale]") {
  Fixture fixture;
  float scale = 1.0f;
  ui::ImGuiLayerDesc desc = fixture.layerDesc();
  desc.displayScale = [&scale] { return scale; };
  ui::ImGuiLayer layer{desc};
  const ui::Theme theme;
  const ImGuiIO &io = ImGui::GetIO();
  CHECK_FALSE(io.ConfigDpiScaleFonts);
  CHECK_FALSE(io.ConfigDpiScaleViewports);

  const SizesSeen base = frame(layer);
  CHECK(ui::currentScale() == 1.0f);
  CHECK(base.fontScaleDpi == 1.0f);
  CHECK(base.itemSpacingX == theme.itemSpacing.x);
  CHECK(base.windowPaddingX == theme.windowPadding.x);

  for (const float next : {1.25f, 1.5f, 2.0f}) {
    scale = next;
    const SizesSeen scaled = frame(layer);
    CAPTURE(next);
    ImGuiStyle expected;
    ui::applyTheme(expected, theme, next, false);
    CHECK(ui::currentScale() == next);
    CHECK(scaled.fontScaleDpi == next);
    CHECK(scaled.fontScaleMain == 1.0f);
    CHECK(scaled.fontSize == Catch::Approx(base.fontSize * next).margin(0.5f)); // Dear ImGui rounds to whole pixels
    CHECK(scaled.windowPaddingX == expected.WindowPadding.x);
    CHECK(scaled.framePaddingY == expected.FramePadding.y);
    CHECK(scaled.itemSpacingX == expected.ItemSpacing.x);
    CHECK(scaled.indentSpacing == expected.IndentSpacing);
    CHECK(scaled.scrollbarSize == expected.ScrollbarSize);
  }

  // Back and forth ends exactly where it began: the style is rebuilt from the tokens, not scaled by the ratio.
  for (const float next : {1.25f, 2.0f, 1.0f, 1.5f, 1.0f}) {
    scale = next;
    frame(layer);
  }
  CHECK(frame(layer) == base);
  CHECK(ui::currentScale() == 1.0f);
}

TEST_CASE("the style is derived again only when the display scale changes", "[ui][gpu][scale]") {
  Fixture fixture;
  float scale = 1.0f;
  ui::ImGuiLayerDesc desc = fixture.layerDesc();
  desc.displayScale = [&scale] { return scale; };
  ui::ImGuiLayer layer{desc};
  frame(layer);

  ImGui::GetStyle().ItemSpacing = ImVec2{99.0f, 99.0f};
  frame(layer);
  CHECK(ImGui::GetStyle().ItemSpacing.x == 99.0f); // the same scale: left alone

  for (const float ignored : {0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN()}) {
    scale = ignored;
    frame(layer);
    CHECK(ImGui::GetStyle().ItemSpacing.x == 99.0f);
    CHECK(ui::currentScale() == 1.0f);
  }

  scale = 1.5f;
  frame(layer);
  CHECK(ImGui::GetStyle().ItemSpacing.x == ImTrunc(ui::Theme{}.itemSpacing.x * 1.5f));

  scale = 40.0f; // clamped, not followed
  frame(layer);
  CHECK(ui::currentScale() == 4.0f);
  scale = 0.01f;
  frame(layer);
  CHECK(ui::currentScale() == 0.5f);
}

TEST_CASE("a change of the display scale resizes the windows by the ratio", "[ui][gpu][scale]") {
  Fixture fixture;
  float scale = 1.0f;
  ui::ImGuiLayerDesc desc = fixture.layerDesc();
  desc.displayScale = [&scale] { return scale; };
  ui::ImGuiLayer layer{desc};
  frame(layer);
  frame(layer);
  const ImGuiWindow *panel = ImGui::FindWindowByName("panel");
  REQUIRE(panel != nullptr);
  CHECK(panel->Size.x == Catch::Approx(200.0f));
  CHECK(panel->Size.y == Catch::Approx(100.0f));

  scale = 2.0f;
  frame(layer);
  CHECK(panel->Size.x == Catch::Approx(400.0f));
  CHECK(panel->Size.y == Catch::Approx(200.0f));
  CHECK(panel->Pos.x == Catch::Approx(40.0f));

  scale = 1.0f;
  frame(layer);
  CHECK(panel->Size.x == Catch::Approx(200.0f));
  CHECK(panel->Size.y == Catch::Approx(100.0f));
}

TEST_CASE("the layer reads the main window's scale when it is given no source", "[ui][gpu][scale]") {
  Fixture fixture;
  ui::ImGuiLayer layer{fixture.layerDesc()};
  frame(layer);
  const float windowScale = SDL_GetWindowDisplayScale(fixture.window->nativeHandle()) /
                            SDL_GetWindowPixelDensity(fixture.window->nativeHandle());
  CHECK(ui::currentScale() == Catch::Approx(std::clamp(windowScale, 0.5f, 4.0f)));
  CHECK(ImGui::GetStyle().FontScaleDpi == Catch::Approx(ui::currentScale()));
}
