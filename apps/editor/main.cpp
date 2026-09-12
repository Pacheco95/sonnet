#include <sonnet/platform/EntryPoint.h>

#include <sonnet/core/Log.h>
#include <sonnet/platform/Application.h>
#include <sonnet/platform/Event.h>
#include <sonnet/platform/Platform.h>
#include <sonnet/platform/Window.h>
#include <sonnet/rhi/Device.h>

#include <memory>
#include <span>
#include <string_view>
#include <variant>

namespace {

using namespace sonnet;

// M0 shell: a window cleared every frame through the render hardware interface. The editor
// module arrives in M1.
class EditorApp final : public platform::IApplication {
public:
  explicit EditorApp(platform::Platform &platform)
      : m_window(platform.createWindow({.title = "Sonnet Editor", .size = {1280, 720}})),
        m_device(rhi::createDevice({.platform = &platform, .applicationName = "Sonnet Editor"})),
        m_swapchain(m_device->createSwapchain(*m_window)) {
  }

  ~EditorApp() override {
    m_device->waitIdle();
  }

  platform::AppResult iterate() override {
    rhi::ICommandList &commands = m_device->beginFrame();
    if (const auto image = m_swapchain->acquire()) {
      commands.barrier(image->image, rhi::ImageLayout::Undefined, rhi::ImageLayout::ColorAttachment);
      const rhi::ColorAttachment attachment{.image = image->image, .clearColor = {0.1f, 0.1f, 0.12f, 1.0f}};
      commands.beginRendering({&attachment, 1});
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

private:
  // Declared in creation order so the swapchain dies before the device, the device before the window.
  std::unique_ptr<platform::IWindow> m_window;
  std::unique_ptr<rhi::IDevice> m_device;
  std::unique_ptr<rhi::ISwapchain> m_swapchain;
};

} // namespace

std::unique_ptr<platform::IApplication> platform::createApplication(Platform &platform,
                                                                    std::span<const std::string_view>) {
  return std::make_unique<EditorApp>(platform);
}
