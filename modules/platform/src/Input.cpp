#include <sonnet/platform/Input.h>

#include <array>
#include <cstddef>

namespace sonnet::platform {

namespace {

// In enumerator order; Key::Unknown is first and has the empty name.
constexpr std::array<std::string_view, static_cast<std::size_t>(Key::Count)> KeyNames{"",
                                                                                      "A",
                                                                                      "B",
                                                                                      "C",
                                                                                      "D",
                                                                                      "E",
                                                                                      "F",
                                                                                      "G",
                                                                                      "H",
                                                                                      "I",
                                                                                      "J",
                                                                                      "K",
                                                                                      "L",
                                                                                      "M",
                                                                                      "N",
                                                                                      "O",
                                                                                      "P",
                                                                                      "Q",
                                                                                      "R",
                                                                                      "S",
                                                                                      "T",
                                                                                      "U",
                                                                                      "V",
                                                                                      "W",
                                                                                      "X",
                                                                                      "Y",
                                                                                      "Z",
                                                                                      "Digit0",
                                                                                      "Digit1",
                                                                                      "Digit2",
                                                                                      "Digit3",
                                                                                      "Digit4",
                                                                                      "Digit5",
                                                                                      "Digit6",
                                                                                      "Digit7",
                                                                                      "Digit8",
                                                                                      "Digit9",
                                                                                      "F1",
                                                                                      "F2",
                                                                                      "F3",
                                                                                      "F4",
                                                                                      "F5",
                                                                                      "F6",
                                                                                      "F7",
                                                                                      "F8",
                                                                                      "F9",
                                                                                      "F10",
                                                                                      "F11",
                                                                                      "F12",
                                                                                      "Escape",
                                                                                      "Enter",
                                                                                      "Tab",
                                                                                      "Backspace",
                                                                                      "Space",
                                                                                      "Minus",
                                                                                      "Equals",
                                                                                      "LeftBracket",
                                                                                      "RightBracket",
                                                                                      "Backslash",
                                                                                      "Semicolon",
                                                                                      "Apostrophe",
                                                                                      "Grave",
                                                                                      "Comma",
                                                                                      "Period",
                                                                                      "Slash",
                                                                                      "CapsLock",
                                                                                      "PrintScreen",
                                                                                      "ScrollLock",
                                                                                      "Pause",
                                                                                      "Insert",
                                                                                      "Home",
                                                                                      "PageUp",
                                                                                      "Delete",
                                                                                      "End",
                                                                                      "PageDown",
                                                                                      "Right",
                                                                                      "Left",
                                                                                      "Down",
                                                                                      "Up",
                                                                                      "NumLock",
                                                                                      "KeypadDivide",
                                                                                      "KeypadMultiply",
                                                                                      "KeypadMinus",
                                                                                      "KeypadPlus",
                                                                                      "KeypadEnter",
                                                                                      "KeypadPeriod",
                                                                                      "Keypad0",
                                                                                      "Keypad1",
                                                                                      "Keypad2",
                                                                                      "Keypad3",
                                                                                      "Keypad4",
                                                                                      "Keypad5",
                                                                                      "Keypad6",
                                                                                      "Keypad7",
                                                                                      "Keypad8",
                                                                                      "Keypad9",
                                                                                      "LeftCtrl",
                                                                                      "LeftShift",
                                                                                      "LeftAlt",
                                                                                      "LeftSuper",
                                                                                      "RightCtrl",
                                                                                      "RightShift",
                                                                                      "RightAlt",
                                                                                      "RightSuper",
                                                                                      "Menu"};

constexpr std::array<std::string_view, static_cast<std::size_t>(MouseButton::Count)> MouseButtonNames{
    "Left", "Middle", "Right", "X1", "X2"};

} // namespace

std::string_view toString(Key key) noexcept {
  const auto index = static_cast<std::size_t>(key);
  return index < KeyNames.size() ? KeyNames[index] : std::string_view{};
}

std::optional<Key> keyFromName(std::string_view name) noexcept {
  for (std::size_t i = 1; i < KeyNames.size(); ++i) {
    if (KeyNames[i] == name) {
      return static_cast<Key>(i);
    }
  }
  return std::nullopt;
}

std::string_view toString(MouseButton button) noexcept {
  const auto index = static_cast<std::size_t>(button);
  return index < MouseButtonNames.size() ? MouseButtonNames[index] : std::string_view{};
}

std::optional<MouseButton> mouseButtonFromName(std::string_view name) noexcept {
  for (std::size_t i = 0; i < MouseButtonNames.size(); ++i) {
    if (MouseButtonNames[i] == name) {
      return static_cast<MouseButton>(i);
    }
  }
  return std::nullopt;
}

} // namespace sonnet::platform
