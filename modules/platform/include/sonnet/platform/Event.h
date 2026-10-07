#pragma once

#include <sonnet/platform/Input.h>

#include <sonnet/core/Math.h>

#include <cstdint>
#include <string>
#include <variant>

namespace sonnet::platform {

// Events are values the platform pushes up through IApplication::event. Positions are in window
// coordinates, the mouse's and the touches' alike; pixel sizes are the drawable size the swapchain
// has to match.
//
// Window and pointer events say which window they came from. An application with one window can
// ignore it; the editor, whose undocked panels are OS windows of their own, cannot: a position is
// relative to the window it arrived in, and a resize or a close request of a panel's window is not
// the main window's. 0 is a window SDL did not name (a synthesised event), taken as the main one.
using WindowId = std::uint32_t;

struct WindowResized {
  glm::uvec2 pixelSize;
  WindowId window{0};
};
// The window's client area moved; the position is in desktop coordinates.
struct WindowMoved {
  glm::ivec2 position;
  WindowId window{0};
};
struct WindowMinimized {};
struct WindowMaximized {
  WindowId window{0};
};
struct WindowRestored {};
struct WindowFocusChanged {
  bool focused;
  WindowId window{0};
};
struct WindowCloseRequested {
  WindowId window{0};
};
// The OS asked the whole application to quit (last window closed, SIGINT, mobile background kill).
struct QuitRequested {};

struct KeyPressed {
  Key key;
  Modifiers modifiers;
  bool repeat;
};
struct KeyReleased {
  Key key;
  Modifiers modifiers;
};
struct TextInput {
  std::string text; // UTF-8
};

struct MouseMoved {
  glm::vec2 position;
  glm::vec2 delta;
  WindowId window{0};
};
struct MouseButtonPressed {
  MouseButton button;
  glm::vec2 position;
  std::uint8_t clicks;
  WindowId window{0};
};
struct MouseButtonReleased {
  MouseButton button;
  glm::vec2 position;
  WindowId window{0};
};
struct MouseWheel {
  glm::vec2 delta; // +y away from the user
};

// A finger on a touch screen, known by its id from down to up (docs/platform.md, "Events"). A
// cancelled finger ends with a TouchUp.
struct TouchDown {
  std::uint64_t id;
  glm::vec2 position;
  WindowId window{0};
};
struct TouchUp {
  std::uint64_t id;
  glm::vec2 position;
  WindowId window{0};
};
struct TouchMotion {
  std::uint64_t id;
  glm::vec2 position;
  glm::vec2 delta;
  WindowId window{0};
};

// The mobile lifecycle (docs/platform.md, "Lifecycle"). They arrive inside SDL_AppEvent, between
// frames, and the application acts on them before returning: going to the background, release
// what draws to the window and stop the sound; coming back, create them again.
struct WillEnterBackground {};
struct DidEnterForeground {};
// The OS wants memory back: iOS's memory warning, Android's onTrimMemory.
struct LowMemory {};
// The OS is ending the application; SDL ends the loop after this event.
struct Terminating {};

using Event = std::variant<WindowResized, WindowMoved, WindowMinimized, WindowMaximized, WindowRestored,
                           WindowFocusChanged, WindowCloseRequested, QuitRequested, KeyPressed, KeyReleased, TextInput,
                           MouseMoved, MouseButtonPressed, MouseButtonReleased, MouseWheel, TouchDown, TouchUp,
                           TouchMotion, WillEnterBackground, DidEnterForeground, LowMemory, Terminating>;

} // namespace sonnet::platform
