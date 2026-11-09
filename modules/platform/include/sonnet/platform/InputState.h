#pragma once

#include <sonnet/platform/Event.h>
#include <sonnet/platform/Input.h>

#include <sonnet/core/Math.h>

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <span>

namespace sonnet::platform {

// A finger held on the screen: where it is and how far it moved this frame.
struct Touch {
  std::uint64_t id{0};
  glm::vec2 position{0.0f, 0.0f};
  glm::vec2 delta{0.0f, 0.0f};
};

// The keyboard, mouse, touches and the active gamepad as state rather than as a stream of events:
// what is held, what went down or up since the last beginFrame, where the pointer and the fingers
// are, and where the sticks and triggers stand. The application feeds it the events it wants the
// game to see (docs/architecture.md, "Application lifecycle").
class InputState {
public:
  // More fingers than any screen the engine runs on reports; one past it is ignored.
  static constexpr std::size_t MaxTouches = 10;
  // The sticks' radial dead zone: a stick this close to its centre reads as centred. From there
  // the length is rescaled so it starts again at 0 and reaches 1 at StickFull, keeping the
  // direction (docs/platform.md, "Gamepads").
  static constexpr float StickDeadZone = 0.24f;
  static constexpr float StickFull = 0.95f;
  // The same for the triggers, from let go to pulled.
  static constexpr float TriggerDeadZone = 0.12f;
  static constexpr float TriggerFull = 0.95f;

  // Starts a frame: forgets the presses, releases, motion and wheel of the previous one.
  void beginFrame() noexcept;
  void handle(const Event &event) noexcept;
  // Releases everything held, as a lost focus does; the releases show this frame. The sticks and
  // triggers read as let go until their next value arrives; whether a gamepad is connected stays.
  void releaseAll() noexcept;
  // Back to a new state, edges and the pointer lock included, but for whether a gamepad is
  // connected, which only the platform's connection events change: a scene replaced in play.
  void reset() noexcept;
  // Forgets the mouse's motion so far this frame, as beginFrame does, keeping the presses and
  // releases: the motion from before the pointer was locked or let go (MouseLock).
  void clearMotion() noexcept {
    m_mouseDelta = {0.0f, 0.0f};
  }

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
  // The fingers down now, in the order they went down.
  [[nodiscard]] std::span<const Touch> touches() const noexcept {
    return std::span{m_touches}.first(m_touchCount);
  }
  // Whether the pointer is locked to the game (hidden, motion as deltas only): what the
  // application got when it last applied a game's request, not the request itself. Kept across
  // frames and releases; false until the application says otherwise.
  void setMouseLocked(bool locked) noexcept {
    m_mouseLocked = locked;
  }
  [[nodiscard]] bool mouseLocked() const noexcept {
    return m_mouseLocked;
  }

  // Whether a gamepad is the active one now.
  [[nodiscard]] bool gamepadConnected() const noexcept {
    return m_gamepadConnected;
  }
  [[nodiscard]] bool gamepadDown(GamepadButton button) const noexcept;
  [[nodiscard]] bool gamepadPressed(GamepadButton button) const noexcept;
  [[nodiscard]] bool gamepadReleased(GamepadButton button) const noexcept;
  // The sticks after the radial dead zone, no longer than 1, +x right and +y up; a stick pushed
  // part of the way keeps its sub-unit length.
  [[nodiscard]] glm::vec2 leftStick() const noexcept;
  [[nodiscard]] glm::vec2 rightStick() const noexcept;
  // The triggers after their dead zone, from 0 to 1.
  [[nodiscard]] float leftTrigger() const noexcept;
  [[nodiscard]] float rightTrigger() const noexcept;
  // An axis as the platform reported it, before any dead zone; 0 after a release.
  [[nodiscard]] float gamepadAxis(GamepadAxis axis) const noexcept;

private:
  [[nodiscard]] Touch *findTouch(std::uint64_t id) noexcept;

  static constexpr std::size_t KeyCount = static_cast<std::size_t>(Key::Count);
  static constexpr std::size_t ButtonCount = static_cast<std::size_t>(MouseButton::Count);
  static constexpr std::size_t GamepadButtonCount = static_cast<std::size_t>(GamepadButton::Count);
  static constexpr std::size_t GamepadAxisCount = static_cast<std::size_t>(GamepadAxis::Count);

  // Lets go of the gamepad's buttons, the releases showing this frame, and centres its axes.
  void releaseGamepad() noexcept;

  std::bitset<KeyCount> m_keysDown;
  std::bitset<KeyCount> m_keysPressed;
  std::bitset<KeyCount> m_keysReleased;
  std::bitset<ButtonCount> m_buttonsDown;
  std::bitset<ButtonCount> m_buttonsPressed;
  std::bitset<ButtonCount> m_buttonsReleased;
  glm::vec2 m_mousePosition{0.0f, 0.0f};
  glm::vec2 m_mouseDelta{0.0f, 0.0f};
  glm::vec2 m_wheel{0.0f, 0.0f};
  std::array<Touch, MaxTouches> m_touches{};
  std::size_t m_touchCount{0};
  bool m_mouseLocked{false};
  std::bitset<GamepadButtonCount> m_padDown;
  std::bitset<GamepadButtonCount> m_padPressed;
  std::bitset<GamepadButtonCount> m_padReleased;
  std::array<float, GamepadAxisCount> m_padAxes{};
  bool m_gamepadConnected{false};
};

} // namespace sonnet::platform
