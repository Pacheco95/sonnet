#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace sonnet::platform {

// Physical key positions (US layout names), independent of the active keyboard layout.
enum class Key : std::uint8_t {
  Unknown,
  A,
  B,
  C,
  D,
  E,
  F,
  G,
  H,
  I,
  J,
  K,
  L,
  M,
  N,
  O,
  P,
  Q,
  R,
  S,
  T,
  U,
  V,
  W,
  X,
  Y,
  Z,
  Digit0,
  Digit1,
  Digit2,
  Digit3,
  Digit4,
  Digit5,
  Digit6,
  Digit7,
  Digit8,
  Digit9,
  F1,
  F2,
  F3,
  F4,
  F5,
  F6,
  F7,
  F8,
  F9,
  F10,
  F11,
  F12,
  Escape,
  Enter,
  Tab,
  Backspace,
  Space,
  Minus,
  Equals,
  LeftBracket,
  RightBracket,
  Backslash,
  Semicolon,
  Apostrophe,
  Grave,
  Comma,
  Period,
  Slash,
  CapsLock,
  PrintScreen,
  ScrollLock,
  Pause,
  Insert,
  Home,
  PageUp,
  Delete,
  End,
  PageDown,
  Right,
  Left,
  Down,
  Up,
  NumLock,
  KeypadDivide,
  KeypadMultiply,
  KeypadMinus,
  KeypadPlus,
  KeypadEnter,
  KeypadPeriod,
  Keypad0,
  Keypad1,
  Keypad2,
  Keypad3,
  Keypad4,
  Keypad5,
  Keypad6,
  Keypad7,
  Keypad8,
  Keypad9,
  LeftCtrl,
  LeftShift,
  LeftAlt,
  LeftSuper,
  RightCtrl,
  RightShift,
  RightAlt,
  RightSuper,
  Menu,
  Count
};

enum class MouseButton : std::uint8_t {
  Left,
  Middle,
  Right,
  X1,
  X2,
  Count
};

enum class Modifiers : std::uint8_t {
  None = 0,
  Shift = 1 << 0,
  Ctrl = 1 << 1,
  Alt = 1 << 2,
  Super = 1 << 3,
  CapsLock = 1 << 4,
  NumLock = 1 << 5,
};

constexpr Modifiers operator|(Modifiers a, Modifiers b) noexcept {
  return static_cast<Modifiers>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}
constexpr Modifiers operator&(Modifiers a, Modifiers b) noexcept {
  return static_cast<Modifiers>(static_cast<std::uint8_t>(a) & static_cast<std::uint8_t>(b));
}
constexpr Modifiers &operator|=(Modifiers &a, Modifiers b) noexcept {
  return a = a | b;
}
constexpr bool has(Modifiers set, Modifiers flag) noexcept {
  return (set & flag) == flag;
}

// The enumerator names ("A", "Space", "LeftShift", "Left"), which scripts use to name keys and
// buttons. Unknown names give nullopt; Key::Unknown and the Count values have no name.
[[nodiscard]] std::string_view toString(Key key) noexcept;
[[nodiscard]] std::optional<Key> keyFromName(std::string_view name) noexcept;
[[nodiscard]] std::string_view toString(MouseButton button) noexcept;
[[nodiscard]] std::optional<MouseButton> mouseButtonFromName(std::string_view name) noexcept;

} // namespace sonnet::platform
