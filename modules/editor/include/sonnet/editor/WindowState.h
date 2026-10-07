#pragma once

#include <sonnet/core/Error.h>
#include <sonnet/core/Math.h>
#include <sonnet/platform/Window.h>

#include <filesystem>
#include <optional>
#include <span>
#include <string>

namespace sonnet::editor {

// Where the main window was when the editor closed, kept as `window.json` beside `layout.ini`. It
// is read in `main` before the window exists, so it is not part of `Preferences`, which `Editor`
// loads and rewrites whole once the window is up.
//
// The display is stored by what survives a restart, its name and bounds, since SDL's display ids do
// not, and the position is relative to that display's top-left so it follows the display when the
// arrangement of the monitors changes.
struct WindowState {
  glm::uvec2 size{1600, 900}; // logical; the rectangle a restore returns to when maximized
  glm::ivec2 position{0, 0};  // relative to the display's top-left
  bool maximized{false};
  std::string display;
  glm::ivec2 displayPosition{0, 0};
  glm::uvec2 displaySize{0, 0};

  // Nothing for a missing file, one that is not JSON, or one with a size no window has.
  [[nodiscard]] static std::optional<WindowState> load(const std::filesystem::path &file);
  [[nodiscard]] core::Result<void> save(const std::filesystem::path &file) const;

  // The same place, within what a window manager moves a window by when it opens it (its title
  // bar, a snap): the positions within `Tolerance` pixels count as equal.
  static constexpr int Tolerance = 48;
  [[nodiscard]] bool sameAs(const WindowState &other) const;
};

// What the window is created with, from the saved state and the displays that are connected now.
struct WindowPlacement {
  glm::uvec2 size{1600, 900};
  std::optional<glm::ivec2> position; // desktop coordinates; empty leaves it to the system
  bool maximized{false};
  // The saved display is gone, or the saved rectangle would be off every display: the window opens
  // centred on the primary display. `state` then names the primary display, which is not the
  // user's choice, so a window left as it opened must not overwrite the saved state.
  bool fallback{false};
  WindowState state; // what the window's state is as placed, for sameAs

  void applyTo(platform::WindowDesc &desc) const;
};

// `canPosition` is Platform::canPositionWindows: Wayland keeps the size and the maximized flag
// and leaves the position to the compositor.
[[nodiscard]] WindowPlacement placeWindow(const std::optional<WindowState> &saved,
                                          std::span<const platform::Display> displays, bool canPosition);

// The state of a window that is on `display`, its restored rectangle being `restored`.
[[nodiscard]] WindowState makeWindowState(const platform::WindowRect &restored, bool maximized,
                                          const platform::Display &display);
// Nothing when the window names no display.
[[nodiscard]] std::optional<WindowState> captureWindowState(const platform::IWindow &window);

} // namespace sonnet::editor
