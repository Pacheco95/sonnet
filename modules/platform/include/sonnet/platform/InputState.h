#pragma once

#include <sonnet/platform/Event.h>
#include <sonnet/platform/Input.h>

#include <sonnet/core/Math.h>

#include <bitset>
#include <cstddef>

namespace sonnet::platform {

// The keyboard and mouse as state rather than as a stream of events: what is held, what went
// down or up since the last beginFrame, and where the pointer is. The application feeds it the
// events it wants the game to see (docs/architecture.md, "Application lifecycle").
class InputState {
public:
  // Starts a frame: forgets the presses, releases, motion and wheel of the previous one.
  void beginFrame() noexcept;
  void handle(const Event &event) noexcept;
  // Releases everything held, as a lost focus does; the releases show this frame.
  void releaseAll() noexcept;

  [[nodiscard]] bool keyDown(Key key) const noexcept;
  [[nodiscard]] bool keyPressed(Key key) const noexcept;
  [[nodiscard]] bool keyReleased(Key key) const noexcept;
  [[nodiscard]] bool mouseDown(MouseButton button) const noexcept;
  [[nodiscard]] bool mousePressed(MouseButton button) const noexcept;
  [[nodiscard]] bool mouseReleased(MouseButton button) const noexcept;
  [[nodiscard]] glm::vec2 mousePosition() const noexcept {
    return m_mousePosition;
  }
  [[nodiscard]] glm::vec2 mouseDelta() const noexcept {
    return m_mouseDelta;
  }
  [[nodiscard]] glm::vec2 wheel() const noexcept {
    return m_wheel;
  }

private:
  static constexpr std::size_t KeyCount = static_cast<std::size_t>(Key::Count);
  static constexpr std::size_t ButtonCount = static_cast<std::size_t>(MouseButton::Count);

  std::bitset<KeyCount> m_keysDown;
  std::bitset<KeyCount> m_keysPressed;
  std::bitset<KeyCount> m_keysReleased;
  std::bitset<ButtonCount> m_buttonsDown;
  std::bitset<ButtonCount> m_buttonsPressed;
  std::bitset<ButtonCount> m_buttonsReleased;
  glm::vec2 m_mousePosition{0.0f, 0.0f};
  glm::vec2 m_mouseDelta{0.0f, 0.0f};
  glm::vec2 m_wheel{0.0f, 0.0f};
};

} // namespace sonnet::platform
