#include <sonnet/platform/EntryPoint.h>

#include <sonnet/editor/Editor.h>
#include <sonnet/platform/Application.h>
#include <sonnet/platform/Event.h>
#include <sonnet/platform/Platform.h>
#include <sonnet/platform/Window.h>
#include <sonnet/rhi/Device.h>
#include <sonnet/rhi/Swapchain.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <span>
#include <string_view>
#include <variant>

namespace {

using namespace sonnet;

// The frame order lives here (docs/architecture.md, "Application lifecycle"): events, then
// update, then the render graph into the acquired swapchain image, then present.
class EditorApp final : public platform::IApplication {
public:
  explicit EditorApp(platform::Platform &platform)
      : m_window(platform.createWindow({.title = "Sonnet Editor", .size = {1600, 900}})),
        m_device(rhi::createDevice({.platform = &platform, .applicationName = "Sonnet Editor"})),
        m_swapchain(m_device->createSwapchain(*m_window)),
        m_editor(std::make_unique<editor::Editor>(platform, *m_window, *m_device, *m_swapchain)) {
  }

  ~EditorApp() override {
    m_device->waitIdle();
  }

  platform::AppResult iterate() override {
    const auto now = std::chrono::steady_clock::now();
    // A long stall (a breakpoint, a window drag) must not fling the camera across the scene.
    const float dt = std::min(std::chrono::duration<float>(now - m_lastFrame).count(), 0.1f);
    m_lastFrame = now;

    m_editor->update(dt);
    rhi::ICommandList &commands = m_device->beginFrame();
    const std::optional<rhi::SwapchainImage> image = m_swapchain->acquire();
    m_editor->render(commands, image);
    m_device->endFrame();
    m_editor->afterPresent();
    return m_editor->quitRequested() ? platform::AppResult::Success : platform::AppResult::Continue;
  }

  platform::AppResult event(const platform::Event &event) override {
    if (std::holds_alternative<platform::WindowCloseRequested>(event) ||
        std::holds_alternative<platform::QuitRequested>(event)) {
      return platform::AppResult::Success;
    }
    if (std::holds_alternative<platform::WindowResized>(event)) {
      m_swapchain->requestResize();
    }
    m_editor->event(event);
    return platform::AppResult::Continue;
  }

  void nativeEvent(const SDL_Event &event) override {
    m_editor->nativeEvent(event);
  }

private:
  // Declared in creation order: the editor dies before the swapchain, the swapchain before the
  // device, the device before the window.
  std::unique_ptr<platform::IWindow> m_window;
  std::unique_ptr<rhi::IDevice> m_device;
  std::unique_ptr<rhi::ISwapchain> m_swapchain;
  std::unique_ptr<editor::Editor> m_editor;
  std::chrono::steady_clock::time_point m_lastFrame{std::chrono::steady_clock::now()};
};

} // namespace

std::unique_ptr<platform::IApplication> platform::createApplication(Platform &platform,
                                                                    std::span<const std::string_view>) {
  return std::make_unique<EditorApp>(platform);
}
