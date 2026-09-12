#include "SdlEvents.h"

#include <SDL3/SDL_mouse.h>

#include <array>
#include <utility>

namespace sonnet::platform {

namespace {

constexpr std::array<Key, SDL_SCANCODE_COUNT> makeKeyTable() {
  std::array<Key, SDL_SCANCODE_COUNT> table{};
  table.fill(Key::Unknown);
  constexpr std::pair<SDL_Scancode, Key> pairs[] = {
      {SDL_SCANCODE_A, Key::A},
      {SDL_SCANCODE_B, Key::B},
      {SDL_SCANCODE_C, Key::C},
      {SDL_SCANCODE_D, Key::D},
      {SDL_SCANCODE_E, Key::E},
      {SDL_SCANCODE_F, Key::F},
      {SDL_SCANCODE_G, Key::G},
      {SDL_SCANCODE_H, Key::H},
      {SDL_SCANCODE_I, Key::I},
      {SDL_SCANCODE_J, Key::J},
      {SDL_SCANCODE_K, Key::K},
      {SDL_SCANCODE_L, Key::L},
      {SDL_SCANCODE_M, Key::M},
      {SDL_SCANCODE_N, Key::N},
      {SDL_SCANCODE_O, Key::O},
      {SDL_SCANCODE_P, Key::P},
      {SDL_SCANCODE_Q, Key::Q},
      {SDL_SCANCODE_R, Key::R},
      {SDL_SCANCODE_S, Key::S},
      {SDL_SCANCODE_T, Key::T},
      {SDL_SCANCODE_U, Key::U},
      {SDL_SCANCODE_V, Key::V},
      {SDL_SCANCODE_W, Key::W},
      {SDL_SCANCODE_X, Key::X},
      {SDL_SCANCODE_Y, Key::Y},
      {SDL_SCANCODE_Z, Key::Z},
      {SDL_SCANCODE_0, Key::Digit0},
      {SDL_SCANCODE_1, Key::Digit1},
      {SDL_SCANCODE_2, Key::Digit2},
      {SDL_SCANCODE_3, Key::Digit3},
      {SDL_SCANCODE_4, Key::Digit4},
      {SDL_SCANCODE_5, Key::Digit5},
      {SDL_SCANCODE_6, Key::Digit6},
      {SDL_SCANCODE_7, Key::Digit7},
      {SDL_SCANCODE_8, Key::Digit8},
      {SDL_SCANCODE_9, Key::Digit9},
      {SDL_SCANCODE_F1, Key::F1},
      {SDL_SCANCODE_F2, Key::F2},
      {SDL_SCANCODE_F3, Key::F3},
      {SDL_SCANCODE_F4, Key::F4},
      {SDL_SCANCODE_F5, Key::F5},
      {SDL_SCANCODE_F6, Key::F6},
      {SDL_SCANCODE_F7, Key::F7},
      {SDL_SCANCODE_F8, Key::F8},
      {SDL_SCANCODE_F9, Key::F9},
      {SDL_SCANCODE_F10, Key::F10},
      {SDL_SCANCODE_F11, Key::F11},
      {SDL_SCANCODE_F12, Key::F12},
      {SDL_SCANCODE_ESCAPE, Key::Escape},
      {SDL_SCANCODE_RETURN, Key::Enter},
      {SDL_SCANCODE_TAB, Key::Tab},
      {SDL_SCANCODE_BACKSPACE, Key::Backspace},
      {SDL_SCANCODE_SPACE, Key::Space},
      {SDL_SCANCODE_MINUS, Key::Minus},
      {SDL_SCANCODE_EQUALS, Key::Equals},
      {SDL_SCANCODE_LEFTBRACKET, Key::LeftBracket},
      {SDL_SCANCODE_RIGHTBRACKET, Key::RightBracket},
      {SDL_SCANCODE_BACKSLASH, Key::Backslash},
      {SDL_SCANCODE_SEMICOLON, Key::Semicolon},
      {SDL_SCANCODE_APOSTROPHE, Key::Apostrophe},
      {SDL_SCANCODE_GRAVE, Key::Grave},
      {SDL_SCANCODE_COMMA, Key::Comma},
      {SDL_SCANCODE_PERIOD, Key::Period},
      {SDL_SCANCODE_SLASH, Key::Slash},
      {SDL_SCANCODE_CAPSLOCK, Key::CapsLock},
      {SDL_SCANCODE_PRINTSCREEN, Key::PrintScreen},
      {SDL_SCANCODE_SCROLLLOCK, Key::ScrollLock},
      {SDL_SCANCODE_PAUSE, Key::Pause},
      {SDL_SCANCODE_INSERT, Key::Insert},
      {SDL_SCANCODE_HOME, Key::Home},
      {SDL_SCANCODE_PAGEUP, Key::PageUp},
      {SDL_SCANCODE_DELETE, Key::Delete},
      {SDL_SCANCODE_END, Key::End},
      {SDL_SCANCODE_PAGEDOWN, Key::PageDown},
      {SDL_SCANCODE_RIGHT, Key::Right},
      {SDL_SCANCODE_LEFT, Key::Left},
      {SDL_SCANCODE_DOWN, Key::Down},
      {SDL_SCANCODE_UP, Key::Up},
      {SDL_SCANCODE_NUMLOCKCLEAR, Key::NumLock},
      {SDL_SCANCODE_KP_DIVIDE, Key::KeypadDivide},
      {SDL_SCANCODE_KP_MULTIPLY, Key::KeypadMultiply},
      {SDL_SCANCODE_KP_MINUS, Key::KeypadMinus},
      {SDL_SCANCODE_KP_PLUS, Key::KeypadPlus},
      {SDL_SCANCODE_KP_ENTER, Key::KeypadEnter},
      {SDL_SCANCODE_KP_PERIOD, Key::KeypadPeriod},
      {SDL_SCANCODE_KP_0, Key::Keypad0},
      {SDL_SCANCODE_KP_1, Key::Keypad1},
      {SDL_SCANCODE_KP_2, Key::Keypad2},
      {SDL_SCANCODE_KP_3, Key::Keypad3},
      {SDL_SCANCODE_KP_4, Key::Keypad4},
      {SDL_SCANCODE_KP_5, Key::Keypad5},
      {SDL_SCANCODE_KP_6, Key::Keypad6},
      {SDL_SCANCODE_KP_7, Key::Keypad7},
      {SDL_SCANCODE_KP_8, Key::Keypad8},
      {SDL_SCANCODE_KP_9, Key::Keypad9},
      {SDL_SCANCODE_LCTRL, Key::LeftCtrl},
      {SDL_SCANCODE_LSHIFT, Key::LeftShift},
      {SDL_SCANCODE_LALT, Key::LeftAlt},
      {SDL_SCANCODE_LGUI, Key::LeftSuper},
      {SDL_SCANCODE_RCTRL, Key::RightCtrl},
      {SDL_SCANCODE_RSHIFT, Key::RightShift},
      {SDL_SCANCODE_RALT, Key::RightAlt},
      {SDL_SCANCODE_RGUI, Key::RightSuper},
      {SDL_SCANCODE_APPLICATION, Key::Menu},
  };
  for (const auto &[scancode, key] : pairs) {
    table[static_cast<std::size_t>(scancode)] = key;
  }
  return table;
}

constexpr std::array<Key, SDL_SCANCODE_COUNT> KeyTable = makeKeyTable();

MouseButton mouseButtonFromSdl(Uint8 button) noexcept {
  switch (button) {
  case SDL_BUTTON_LEFT:
    return MouseButton::Left;
  case SDL_BUTTON_MIDDLE:
    return MouseButton::Middle;
  case SDL_BUTTON_RIGHT:
    return MouseButton::Right;
  case SDL_BUTTON_X1:
    return MouseButton::X1;
  case SDL_BUTTON_X2:
    return MouseButton::X2;
  default:
    return MouseButton::Count;
  }
}

} // namespace

Key keyFromScancode(SDL_Scancode scancode) noexcept {
  const auto index = static_cast<std::size_t>(scancode);
  return index < KeyTable.size() ? KeyTable[index] : Key::Unknown;
}

Modifiers modifiersFromSdl(SDL_Keymod mods) noexcept {
  Modifiers out = Modifiers::None;
  if ((mods & SDL_KMOD_SHIFT) != 0) {
    out |= Modifiers::Shift;
  }
  if ((mods & SDL_KMOD_CTRL) != 0) {
    out |= Modifiers::Ctrl;
  }
  if ((mods & SDL_KMOD_ALT) != 0) {
    out |= Modifiers::Alt;
  }
  if ((mods & SDL_KMOD_GUI) != 0) {
    out |= Modifiers::Super;
  }
  if ((mods & SDL_KMOD_CAPS) != 0) {
    out |= Modifiers::CapsLock;
  }
  if ((mods & SDL_KMOD_NUM) != 0) {
    out |= Modifiers::NumLock;
  }
  return out;
}

std::optional<Event> translateEvent(const SDL_Event &event) {
  switch (event.type) {
  case SDL_EVENT_QUIT:
    return QuitRequested{};
  case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    return WindowResized{{static_cast<unsigned>(event.window.data1), static_cast<unsigned>(event.window.data2)}};
  case SDL_EVENT_WINDOW_MINIMIZED:
    return WindowMinimized{};
  case SDL_EVENT_WINDOW_RESTORED:
    return WindowRestored{};
  case SDL_EVENT_WINDOW_FOCUS_GAINED:
    return WindowFocusChanged{true};
  case SDL_EVENT_WINDOW_FOCUS_LOST:
    return WindowFocusChanged{false};
  case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
    return WindowCloseRequested{};
  case SDL_EVENT_KEY_DOWN:
    return KeyPressed{keyFromScancode(event.key.scancode), modifiersFromSdl(event.key.mod), event.key.repeat};
  case SDL_EVENT_KEY_UP:
    return KeyReleased{keyFromScancode(event.key.scancode), modifiersFromSdl(event.key.mod)};
  case SDL_EVENT_TEXT_INPUT:
    return TextInput{event.text.text};
  case SDL_EVENT_MOUSE_MOTION:
    return MouseMoved{{event.motion.x, event.motion.y}, {event.motion.xrel, event.motion.yrel}};
  case SDL_EVENT_MOUSE_BUTTON_DOWN:
  case SDL_EVENT_MOUSE_BUTTON_UP: {
    const MouseButton button = mouseButtonFromSdl(event.button.button);
    if (button == MouseButton::Count) {
      return std::nullopt;
    }
    const glm::vec2 position{event.button.x, event.button.y};
    if (event.button.down) {
      return MouseButtonPressed{button, position, event.button.clicks};
    }
    return MouseButtonReleased{button, position};
  }
  case SDL_EVENT_MOUSE_WHEEL: {
    const float sign = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
    return MouseWheel{{event.wheel.x * sign, event.wheel.y * sign}};
  }
  default:
    return std::nullopt;
  }
}

} // namespace sonnet::platform
