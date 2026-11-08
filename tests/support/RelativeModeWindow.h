#pragma once

// A headless window with a relative mouse mode the test decides, for the suites that lock the
// pointer (docs/platform.md, "Mouse lock"): the offscreen driver has no relative mode, and a
// success there would prove nothing about a desktop's anyway. Everything else is the real window's.

#include <sonnet/platform/Window.h>

namespace sonnet::test {

class RelativeModeWindow final : public platform::IWindow {
public:
  explicit RelativeModeWindow(platform::IWindow &window) : m_window(window) {
  }

  // Whether a request to enable the mode succeeds; disabling always does.
  bool supported{true};
  // How many times the mode was asked for, either way.
  int requests{0};

  [[nodiscard]] glm::uvec2 size() const override {
    return m_window.size();
  }
  [[nodiscard]] glm::uvec2 pixelSize() const override {
    return m_window.pixelSize();
  }
  [[nodiscard]] bool isMinimized() const override {
    return m_window.isMinimized();
  }
  [[nodiscard]] bool isMaximized() const override {
    return m_window.isMaximized();
  }
  [[nodiscard]] glm::ivec2 position() const override {
    return m_window.position();
  }
  [[nodiscard]] platform::WindowRect restoredRect() const override {
    return m_window.restoredRect();
  }
  [[nodiscard]] std::optional<platform::Display> display() const override {
    return m_window.display();
  }
  [[nodiscard]] platform::WindowRect safeArea() const override {
    return m_window.safeArea();
  }
  [[nodiscard]] std::string_view title() const override {
    return m_window.title();
  }
  void setTitle(std::string_view title) override {
    m_window.setTitle(title);
  }
  void show() override {
    m_window.show();
  }
  bool setRelativeMouseMode(bool enabled) override {
    ++requests;
    if (enabled && !supported) {
      return false;
    }
    m_relative = enabled;
    return true;
  }
  [[nodiscard]] bool relativeMouseMode() const override {
    return m_relative;
  }
  [[nodiscard]] VkSurfaceKHR createVulkanSurface(VkInstance instance) const override {
    return m_window.createVulkanSurface(instance);
  }
  [[nodiscard]] SDL_Window *nativeHandle() const override {
    return m_window.nativeHandle();
  }

private:
  platform::IWindow &m_window;
  bool m_relative{false};
};

} // namespace sonnet::test
