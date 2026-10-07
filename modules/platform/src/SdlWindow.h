#pragma once

#include "RestoredRect.h"

#include <sonnet/platform/Window.h>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>

#include <string>

namespace sonnet::platform {

class SdlWindow final : public IWindow {
public:
  explicit SdlWindow(const WindowDesc &desc);
  ~SdlWindow() override;
  SdlWindow(const SdlWindow &) = delete;
  SdlWindow &operator=(const SdlWindow &) = delete;

  [[nodiscard]] glm::uvec2 size() const override;
  [[nodiscard]] glm::uvec2 pixelSize() const override;
  [[nodiscard]] bool isMinimized() const override;
  [[nodiscard]] bool isMaximized() const override;
  [[nodiscard]] glm::ivec2 position() const override;
  [[nodiscard]] WindowRect restoredRect() const override;
  [[nodiscard]] std::optional<Display> display() const override;
  [[nodiscard]] std::string_view title() const override;
  void setTitle(std::string_view title) override;
  void show() override;
  bool setRelativeMouseMode(bool enabled) override;
  [[nodiscard]] bool relativeMouseMode() const override;
  [[nodiscard]] VkSurfaceKHR createVulkanSurface(VkInstance instance) const override;
  [[nodiscard]] SDL_Window *nativeHandle() const override {
    return m_window;
  }

private:
  // Called as SDL queues an event, before the application sees it, so the restored rectangle is
  // current even for a window the window manager maximized.
  static bool watchEvent(void *userdata, SDL_Event *event);

  SDL_Window *m_window{nullptr};
  RestoredRect m_restored;
  std::string m_title;
  [[maybe_unused]] float m_frameRate{0.0F}; // read on Android only
};

} // namespace sonnet::platform
