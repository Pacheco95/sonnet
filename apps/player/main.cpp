#include <sonnet/platform/EntryPoint.h>

#include <sonnet/assets/Bundle.h>
#include <sonnet/core/Error.h>
#include <sonnet/core/Log.h>
#include <sonnet/platform/Application.h>
#include <sonnet/platform/Event.h>
#include <sonnet/platform/Platform.h>
#include <sonnet/platform/Window.h>
#include <sonnet/rhi/Device.h>
#include <sonnet/rhi/Swapchain.h>
#include <sonnet/runtime/Capture.h>
#include <sonnet/runtime/FrameTimes.h>
#include <sonnet/runtime/Game.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <print>
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
[[nodiscard]] std::filesystem::path contentPath(const std::filesystem::path &argument) {
  if (!argument.empty()) {
    return std::filesystem::absolute(argument);
  }
  return std::filesystem::path{std::string{"game"} + std::string{assets::BundleExtension}};
}

// A packed Vulkan version as the device's log line writes it.
[[nodiscard]] std::string vulkanVersion(std::uint32_t version) {
  return std::format("{}.{}.{}", (version >> 22U) & 0x7FU, (version >> 12U) & 0x3FFU, version & 0xFFFU);
}

[[nodiscard]] runtime::GameDesc gameDesc(const runtime::CommandLine &line) {
  runtime::GameDesc desc;
  // A capture run draws its settle frames before it plays, as the editor's does.
  desc.paused = line.capture.has_value();
  return desc;
}

// Ends the run on its first iteration: --help, or a command line that does not parse. The
// window and device are never created for it.
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

// The frame order lives here (docs/architecture.md, "Application lifecycle"), the same steps as
// the editor's without the UI: events, update, the graph into the acquired image, present, and
// then the screenshot a capture asked the frame for.
class PlayerApp final : public platform::IApplication {
public:
  PlayerApp(platform::Platform &platform, const runtime::CommandLine &line)
      : m_window(platform.createWindow({.title = "Sonnet", .size = {1280, 720}})),
        m_device(rhi::createDevice({.platform = &platform, .applicationName = "Sonnet Player"})),
        m_swapchain(m_device->createSwapchain(*m_window)),
        m_game(std::make_unique<runtime::Game>(*m_window, *m_device, *m_swapchain, gameDesc(line))) {
    if (line.capture) {
      // A relative screenshot goes where a phone lets the player write (docs/player.md, "Capture
      // runs"). The device line is the report's, so a run's log says where it ran.
      runtime::CaptureOptions options = *line.capture;
      runtime::resolveOutputs(options, platform.prefPath("sonnet", "player"));
      const rhi::DeviceInfo &info = m_device->info();
      SONNET_LOG_INFO("capture run on \"{}\", Vulkan {}, writing {}", info.deviceName, vulkanVersion(info.apiVersion),
                      options.viewport.string());
      m_capture.emplace(std::move(options));
    }
    const std::filesystem::path content = contentPath(line.content);
    auto opened = m_game->open(content);
    // --scene alone: another of the game's scenes instead of its start scene.
    if (opened && !line.scene.empty()) {
      opened = m_game->openScene(line.scene);
    }
    if (!opened) {
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
    const float interval = std::chrono::duration<float>(now - m_lastFrame).count();
    // A long stall must not be simulated in one step; the world's accumulator caps the catch-up
    // too, but the frame's own delta is clamped first (ADR-0009). Time in the background is not
    // a stall: coming back restarts the clock. A capture steps at a fixed rate instead, so it
    // plays the same however fast frames are drawn.
    const float dt = m_capture ? runtime::CaptureRun::FrameSeconds : std::min(interval, 0.1f);
    m_lastFrame = now;
    if (m_swapchain->suspended()) {
      ++m_backgroundFrames;
    }

    m_game->update(dt);
    const auto updated = std::chrono::steady_clock::now();
    rhi::ICommandList &commands = m_device->beginFrame();
    const std::optional<rhi::SwapchainImage> image = m_swapchain->acquire();
    const auto recording = std::chrono::steady_clock::now();
    m_game->render(commands, image);
    const auto recorded = std::chrono::steady_clock::now();
    m_device->endFrame();
    m_game->afterPresent();
    if (!m_capture) {
      return platform::AppResult::Continue;
    }
    // The CPU's own work, the simulation and the recording, without the waits for a frame slot
    // and a swapchain image, which are the GPU's and the display's time. The frame that copies
    // the screenshot out is the capture's work rather than the game's, and is left out.
    const renderer::GraphStatistics &statistics = m_game->graph().statistics();
    if (std::ranges::none_of(statistics.passes, [](const auto &pass) { return pass.name == "screenshot"; })) {
      m_frameTimes.record(std::chrono::duration<float, std::milli>((updated - now) + (recorded - recording)).count(),
                          interval * 1000.0f, statistics);
    }
    switch (m_capture->step(m_captureTarget)) {
    case runtime::CaptureRun::Status::Done:
      SONNET_LOG_INFO("capture frame times {}", m_frameTimes.summary());
      return platform::AppResult::Success;
    case runtime::CaptureRun::Status::Failed:
      SONNET_LOG_INFO("capture frame times {}", m_frameTimes.summary());
      return platform::AppResult::Failure;
    case runtime::CaptureRun::Status::Running:
      break;
    }
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
  runtime::GameCaptureTarget m_captureTarget{*m_game};
  std::optional<runtime::CaptureRun> m_capture;
  runtime::FrameTimes m_frameTimes;
  std::chrono::steady_clock::time_point m_lastFrame{std::chrono::steady_clock::now()};
  std::chrono::steady_clock::time_point m_background;
  std::uint64_t m_backgroundFrames{0};
  bool m_failed{false};
};

} // namespace

std::unique_ptr<platform::IApplication> platform::createApplication(Platform &platform,
                                                                    std::span<const std::string_view> args) {
  // A game and the flags of a capture run, in any order (docs/player.md, "Capture runs"). A
  // mistake goes to the log, which is what a phone shows; --help is for a terminal.
  const core::Result<runtime::CommandLine> line = runtime::parseCommandLine(args, runtime::CaptureApplication::Player);
  if (!line) {
    SONNET_LOG_ERROR("sonnet_player: {}", line.error().message);
    return std::make_unique<ExitApp>(platform::AppResult::Failure);
  }
  if (line->help) {
    std::print("{}", runtime::commandLineUsage(runtime::CaptureApplication::Player));
    return std::make_unique<ExitApp>(platform::AppResult::Success);
  }
  return std::make_unique<PlayerApp>(platform, *line);
}
