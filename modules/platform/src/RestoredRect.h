#pragma once

#include <sonnet/platform/Window.h>

namespace sonnet::platform {

// Follows the rectangle a maximized window returns to. SDL3 has no query for it, so it is the last
// rectangle the window had while neither maximized nor minimized, kept up to date from the moves and
// resizes SDL reports. A window maximized by the window manager is covered too, since only the
// flag decides. Minimized windows are skipped because Windows reports them at -32000, -32000.
class RestoredRect {
public:
  explicit RestoredRect(WindowRect initial) noexcept : m_rect(initial) {
  }

  void moved(glm::ivec2 position, bool frozen) noexcept {
    if (!frozen) {
      m_rect.position = position;
    }
  }
  void resized(glm::uvec2 size, bool frozen) noexcept {
    if (!frozen) {
      m_rect.size = size;
    }
  }
  [[nodiscard]] const WindowRect &rect() const noexcept {
    return m_rect;
  }

private:
  WindowRect m_rect;
};

} // namespace sonnet::platform
