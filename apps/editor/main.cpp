#include <sonnet/platform/EntryPoint.h>

#include <sonnet/core/Log.h>
#include <sonnet/platform/Application.h>
#include <sonnet/platform/Event.h>
#include <sonnet/platform/Platform.h>
#include <sonnet/platform/Window.h>

#include <memory>
#include <span>
#include <string_view>
#include <variant>

namespace {

using namespace sonnet;

// M0 shell: a window and the callback loop. The editor module arrives in M1.
class EditorApp final : public platform::IApplication {
public:
  explicit EditorApp(platform::Platform &platform)
      : m_window(platform.createWindow({.title = "Sonnet Editor", .size = {1280, 720}})) {
  }

  platform::AppResult iterate() override {
    return platform::AppResult::Continue;
  }

  platform::AppResult event(const platform::Event &event) override {
    if (std::holds_alternative<platform::WindowCloseRequested>(event) ||
        std::holds_alternative<platform::QuitRequested>(event)) {
      return platform::AppResult::Success;
    }
    if (const auto *resized = std::get_if<platform::WindowResized>(&event)) {
      SONNET_LOG_DEBUG("resized to {}x{}", resized->pixelSize.x, resized->pixelSize.y);
    }
    return platform::AppResult::Continue;
  }

private:
  std::unique_ptr<platform::IWindow> m_window;
};

} // namespace

std::unique_ptr<platform::IApplication> platform::createApplication(Platform &platform,
                                                                    std::span<const std::string_view>) {
  return std::make_unique<EditorApp>(platform);
}
