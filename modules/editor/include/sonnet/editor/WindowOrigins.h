#pragma once

#include <sonnet/core/Math.h>
#include <sonnet/platform/Event.h>

#include <unordered_map>

namespace sonnet::editor {

// Where each OS window's top-left corner is in ImGui's screen space. A pointer event's position is
// relative to the window it arrived in, and ImGui's panels (the Game view among them) are in screen
// space, so a panel undocked into a window of its own needs that window's origin, not the main
// one's. The editor refreshes it from ImGui's viewports every frame; a window it does not know, and
// the unnamed window 0, are taken as the main one.
class WindowOrigins {
public:
  void clear() noexcept {
    m_origins.clear();
  }
  void set(platform::WindowId window, glm::vec2 origin) {
    m_origins[window] = origin;
  }
  void setMain(glm::vec2 origin) noexcept {
    m_main = origin;
  }
  [[nodiscard]] glm::vec2 origin(platform::WindowId window) const {
    const auto found = window == 0 ? m_origins.end() : m_origins.find(window);
    return found != m_origins.end() ? found->second : m_main;
  }
  // An event's position, in window coordinates, relative to the top-left of a panel's image at
  // `imageOrigin` in screen space.
  [[nodiscard]] glm::vec2 relativeToImage(platform::WindowId window, glm::vec2 position, glm::vec2 imageOrigin) const {
    return position + origin(window) - imageOrigin;
  }

private:
  std::unordered_map<platform::WindowId, glm::vec2> m_origins;
  glm::vec2 m_main{0.0f, 0.0f};
};

} // namespace sonnet::editor
