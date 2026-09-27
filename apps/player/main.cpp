#include <sonnet/platform/EntryPoint.h>

#include <sonnet/assets/Bundle.h>
#include <sonnet/core/Log.h>
#include <sonnet/platform/Application.h>
#include <sonnet/platform/Event.h>
#include <sonnet/platform/Platform.h>
#include <sonnet/platform/Window.h>
#include <sonnet/rhi/Device.h>
#include <sonnet/rhi/Swapchain.h>
#include <sonnet/runtime/Game.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace {

using namespace sonnet;

// What to run (docs/player.md): the argument, a path from the working directory, or else
// `game.sbundle` in the content root, which is beside the binary on desktop and the APK's
// assets/ on Android. The argument is made absolute because a relative path is read from the
// content root, not the working directory.
[[nodiscard]] std::filesystem::path contentPath(std::span<const std::string_view> args) {
  if (!args.empty()) {
    return std::filesystem::absolute(std::filesystem::path{args[0]});
  }
  return std::filesystem::path{std::string{"game"} + std::string{assets::BundleExtension}};
}

// The frame order lives here (docs/architecture.md, "Application lifecycle"), the same four
// steps as the editor's without the UI: events, update, the graph into the acquired image,
// present.
class PlayerApp final : public platform::IApplication {
public:
  PlayerApp(platform::Platform &platform, std::span<const std::string_view> args)
      : m_window(platform.createWindow({.title = "Sonnet", .size = {1280, 720}})),
        m_device(rhi::createDevice({.platform = &platform, .applicationName = "Sonnet Player"})),
        m_swapchain(m_device->createSwapchain(*m_window)),
        m_game(std::make_unique<runtime::Game>(*m_window, *m_device, *m_swapchain)) {
    const std::filesystem::path content = contentPath(args);
    if (const auto opened = m_game->open(content); !opened) {
      // Nothing to run is the end of the program, not a window showing an empty scene.
      SONNET_LOG_ERROR("{}", opened.error().toString());
      m_failed = true;
    }
  }

  ~PlayerApp() override {
    m_device->waitIdle();
  }

  platform::AppResult iterate() override {
    if (m_failed) {
      return platform::AppResult::Failure;
    }
    const auto now = std::chrono::steady_clock::now();
    // A long stall must not be simulated in one step; the world's accumulator caps the catch-up
    // too, but the frame's own delta is clamped first (ADR-0009). Time in the background is not
    // a stall: coming back restarts the clock.
    const float dt = std::min(std::chrono::duration<float>(now - m_lastFrame).count(), 0.1f);
    m_lastFrame = now;
    if (m_swapchain->suspended()) {
      ++m_backgroundFrames;
    }

    m_game->update(dt);
    rhi::ICommandList &commands = m_device->beginFrame();
    const std::optional<rhi::SwapchainImage> image = m_swapchain->acquire();
    m_game->render(commands, image);
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
    } else if (std::holds_alternative<platform::WillEnterBackground>(event)) {
      enterBackground();
    } else if (std::holds_alternative<platform::DidEnterForeground>(event)) {
      if (!enterForeground()) {
        return platform::AppResult::Failure;
      }
    } else if (std::holds_alternative<platform::LowMemory>(event)) {
      SONNET_LOG_INFO("the system is low on memory");
    } else if (std::holds_alternative<platform::Terminating>(event)) {
      SONNET_LOG_INFO("the system is ending the application");
    }
    m_game->event(event);
    return platform::AppResult::Continue;
  }

  void nativeEvent(const SDL_Event &) override {
    // Only Dear ImGui's backend needs the untranslated events, and the player has no UI.
  }

private:
  // The mobile lifecycle (docs/player.md, "The lifecycle"). Both arrive inside SDL_AppEvent,
  // between frames. Going to the background releases what draws to the window, which the OS is
  // taking away, and stops the sound.
  void enterBackground() {
    SONNET_LOG_INFO("entering the background");
    m_device->waitIdle();
    m_swapchain->suspend();
    m_game->audio().pause();
    m_background = std::chrono::steady_clock::now();
    m_backgroundFrames = 0;
  }

  // Coming back creates them again from the window. A swapchain that cannot be created again
  // ends the application, which has nothing left to show.
  [[nodiscard]] bool enterForeground() {
    const auto now = std::chrono::steady_clock::now();
    SONNET_LOG_INFO("back from the background after {:.1f} s, {} frames in it",
                    std::chrono::duration<float>(now - m_background).count(), m_backgroundFrames);
    if (const auto resumed = m_swapchain->resume(); !resumed) {
      SONNET_LOG_ERROR("{}", resumed.error().toString());
      return false;
    }
    m_game->audio().resume();
    // The next frame simulates its own time, not the time away.
    m_lastFrame = now;
    return true;
  }

  // Declared in creation order: the game dies before the swapchain, the swapchain before the
  // device, the device before the window.
  std::unique_ptr<platform::IWindow> m_window;
  std::unique_ptr<rhi::IDevice> m_device;
  std::unique_ptr<rhi::ISwapchain> m_swapchain;
  std::unique_ptr<runtime::Game> m_game;
  std::chrono::steady_clock::time_point m_lastFrame{std::chrono::steady_clock::now()};
  std::chrono::steady_clock::time_point m_background;
  std::uint64_t m_backgroundFrames{0};
  bool m_failed{false};
};

} // namespace

std::unique_ptr<platform::IApplication> platform::createApplication(Platform &platform,
                                                                    std::span<const std::string_view> args) {
  return std::make_unique<PlayerApp>(platform, args);
}
