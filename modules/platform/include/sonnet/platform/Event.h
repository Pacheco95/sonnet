#pragma once

#include <sonnet/platform/Input.h>

#include <sonnet/core/Math.h>

#include <cstdint>
#include <string>
#include <variant>

namespace sonnet::platform {

// Events are values the platform pushes up through IApplication::event. Positions are in window
// coordinates, pixel sizes are the drawable size the swapchain has to match.

struct WindowResized {
  glm::uvec2 pixelSize;
};
struct WindowMinimized {};
struct WindowRestored {};
struct WindowFocusChanged {
  bool focused;
};
struct WindowCloseRequested {};
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
};
struct MouseButtonPressed {
  MouseButton button;
  glm::vec2 position;
  std::uint8_t clicks;
};
struct MouseButtonReleased {
  MouseButton button;
  glm::vec2 position;
};
struct MouseWheel {
  glm::vec2 delta; // +y away from the user
};

using Event = std::variant<WindowResized, WindowMinimized, WindowRestored, WindowFocusChanged, WindowCloseRequested,
                           QuitRequested, KeyPressed, KeyReleased, TextInput, MouseMoved, MouseButtonPressed,
                           MouseButtonReleased, MouseWheel>;

} // namespace sonnet::platform
