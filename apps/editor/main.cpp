#include <sonnet/platform/EntryPoint.h>

#include <sonnet/core/Error.h>
#include <sonnet/core/File.h>
#include <sonnet/core/Log.h>
#include <sonnet/platform/Application.h>
#include <sonnet/platform/Event.h>
#include <sonnet/platform/Platform.h>
#include <sonnet/platform/Window.h>
#include <sonnet/rhi/Device.h>

#include <chrono>
#include <cmath>
#include <memory>
#include <span>
#include <string_view>
#include <variant>

namespace {

using namespace sonnet;

// M0 shell: a window with a triangle drawn every frame through the render hardware interface.
// The editor module arrives in M1.
class EditorApp final : public platform::IApplication {
public:
  explicit EditorApp(platform::Platform &platform)
      : m_window(platform.createWindow({.title = "Sonnet Editor", .size = {1280, 720}})),
        m_device(rhi::createDevice({.platform = &platform, .applicationName = "Sonnet Editor"})),
        m_swapchain(m_device->createSwapchain(*m_window)) {
    const auto spirv = core::readFile(platform.basePath() / "shaders" / "triangle.spv");
    if (!spirv) {
      throw core::Exception{spirv.error()};
    }
    const rhi::ShaderHandle shader = m_device->createShader({.spirv = *spirv, .debugName = "triangle"});
    m_pipeline = m_device->createGraphicsPipeline(
        {.shader = shader, .colorFormats = {m_swapchain->format()}, .debugName = "triangle pipeline"});
    m_device->destroyShader(shader);
  }

  ~EditorApp() override {
    m_device->waitIdle();
    m_device->destroyPipeline(m_pipeline);
  }

  platform::AppResult iterate() override {
    rhi::ICommandList &commands = m_device->beginFrame();
    if (const auto image = m_swapchain->acquire()) {
      commands.barrier(image->image, rhi::ImageLayout::Undefined, rhi::ImageLayout::ColorAttachment);
      const rhi::ColorAttachment attachment{.image = image->image, .clearColor = {0.1f, 0.1f, 0.12f, 1.0f}};
      commands.beginRendering({&attachment, 1});
      commands.bindPipeline(m_pipeline);
      const float seconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - m_start).count();
      const float pulse = 0.75f + 0.25f * std::sin(seconds * 2.0f);
      const glm::vec4 tint{pulse, pulse, pulse, 1.0f};
      commands.pushConstants(std::as_bytes(std::span{&tint, 1}));
      commands.draw(3);
      commands.endRendering();
      commands.barrier(image->image, rhi::ImageLayout::ColorAttachment, rhi::ImageLayout::Present);
    }
    m_device->endFrame();
    return platform::AppResult::Continue;
  }

  platform::AppResult event(const platform::Event &event) override {
    if (std::holds_alternative<platform::WindowCloseRequested>(event) ||
        std::holds_alternative<platform::QuitRequested>(event)) {
      return platform::AppResult::Success;
    }
    if (std::holds_alternative<platform::WindowResized>(event)) {
      m_swapchain->requestResize();
    }
    return platform::AppResult::Continue;
  }

  void nativeEvent(const SDL_Event &) override {
    // Dear ImGui consumes these once the ui module exists.
  }

private:
  // Declared in creation order so the swapchain dies before the device, the device before the window.
  std::unique_ptr<platform::IWindow> m_window;
  std::unique_ptr<rhi::IDevice> m_device;
  std::unique_ptr<rhi::ISwapchain> m_swapchain;
  rhi::PipelineHandle m_pipeline;
  std::chrono::steady_clock::time_point m_start{std::chrono::steady_clock::now()};
};

} // namespace

std::unique_ptr<platform::IApplication> platform::createApplication(Platform &platform,
                                                                    std::span<const std::string_view>) {
  return std::make_unique<EditorApp>(platform);
}
