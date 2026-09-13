#pragma once

#include <sonnet/platform/Window.h>

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
  SDL_Window *m_window{nullptr};
  std::string m_title;
};

} // namespace sonnet::platform
