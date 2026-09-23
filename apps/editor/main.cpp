#include <sonnet/platform/EntryPoint.h>

#include <sonnet/core/Log.h>
#include <sonnet/editor/Capture.h>
#include <sonnet/editor/Editor.h>
#include <sonnet/platform/Application.h>
#include <sonnet/platform/Event.h>
#include <sonnet/platform/Platform.h>
#include <sonnet/platform/Window.h>
#include <sonnet/rhi/Device.h>
#include <sonnet/rhi/Swapchain.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <print>
#include <span>
#include <string_view>
#include <variant>

namespace {

using namespace sonnet;

// The flecs explorer is served by Debug builds of the editor (ADR-0003).
#ifdef SONNET_ASSERTS_ENABLED
constexpr bool Explorer = true;
#else
constexpr bool Explorer = false;
#endif

// Ends the run on its first iteration: --help, or a command line that does not parse. The
// editor's window and device are never created for it.
class ExitApp final : public platform::IApplication {
public:
  explicit ExitApp(platform::AppResult result) : m_result(result) {
  }
  platform::AppResult iterate() override {
    return m_result;
  }
  platform::AppResult event(const platform::Event &) override {
    return platform::AppResult::Continue;
  }
  void nativeEvent(const SDL_Event &) override {
  }

private:
  platform::AppResult m_result;
};

// The frame order lives here (docs/architecture.md, "Application lifecycle"): events, then
// update, then the render graph into the acquired swapchain image, then present.
class EditorApp final : public platform::IApplication {
public:
  EditorApp(platform::Platform &platform, const editor::CommandLine &line)
      : m_window(platform.createWindow({.title = "Sonnet Editor", .size = {1600, 900}})),
        m_device(rhi::createDevice({.platform = &platform, .applicationName = "Sonnet Editor"})),
        m_swapchain(m_device->createSwapchain(*m_window)),
        m_editor(std::make_unique<editor::Editor>(platform, *m_window, *m_device, *m_swapchain, Explorer)) {
    if (!line.project.empty()) {
      if (const auto opened = m_editor->openProject(line.project); !opened) {
        SONNET_LOG_ERROR("{}", opened.error().toString());
        m_failed = line.capture.has_value(); // an interactive editor carries on without it
      }
    }
    if (line.capture) {
      m_capture.emplace(*line.capture);
    }
  }

  ~EditorApp() override {
    m_device->waitIdle();
  }

  platform::AppResult iterate() override {
    if (m_failed) {
      return platform::AppResult::Failure;
    }
    const auto now = std::chrono::steady_clock::now();
    // A long stall (a breakpoint, a window drag) must not fling the camera across the scene. A
    // capture steps at a fixed rate instead, so it plays the same however fast frames are drawn.
    const float dt = m_capture ? editor::CaptureRun::FrameSeconds
                               : std::min(std::chrono::duration<float>(now - m_lastFrame).count(), 0.1f);
    m_lastFrame = now;

    m_editor->update(dt);
    rhi::ICommandList &commands = m_device->beginFrame();
    const std::optional<rhi::SwapchainImage> image = m_swapchain->acquire();
    m_editor->render(commands, image);
    m_device->endFrame();
    m_editor->afterPresent();
    if (m_capture) {
      switch (m_capture->step(*m_editor)) {
      case editor::CaptureRun::Status::Done:
        return platform::AppResult::Success;
      case editor::CaptureRun::Status::Failed:
        return platform::AppResult::Failure;
      case editor::CaptureRun::Status::Running:
        break;
      }
    }
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
  std::optional<editor::CaptureRun> m_capture;
  bool m_failed{false};
};

} // namespace

std::unique_ptr<platform::IApplication> platform::createApplication(Platform &platform,
                                                                    std::span<const std::string_view> args) {
  // A project folder, and the flags of a capture run (docs/editor.md, "Screenshots"). Help and
  // mistakes go to the terminal, where whoever typed the command is looking.
  const core::Result<editor::CommandLine> line = editor::parseCommandLine(args);
  if (!line) {
    std::println(stderr, "sonnet_editor: {}", line.error().message);
    return std::make_unique<ExitApp>(platform::AppResult::Failure);
  }
  if (line->help) {
    std::print("{}", editor::commandLineUsage());
    return std::make_unique<ExitApp>(platform::AppResult::Success);
  }
  return std::make_unique<EditorApp>(platform, *line);
}
