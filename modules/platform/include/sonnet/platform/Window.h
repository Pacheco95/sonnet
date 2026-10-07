#pragma once

#include <sonnet/core/Math.h>

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// SDL is the only implementation (ADR-0002) and Dear ImGui's SDL3 backend needs the window it
// wraps, so the interface hands the SDL handle out; declaring it here keeps SDL's headers out.
struct SDL_Window;

namespace sonnet::platform {

// A monitor, in the desktop's logical coordinates. The id is SDL's and lasts only until the display
// is unplugged or the process ends; the name and the bounds are what a restart can match on.
struct Display {
  std::uint32_t id{0};
  std::string name;
  glm::ivec2 position{0, 0}; // top-left of the bounds
  glm::uvec2 size{0, 0};
  glm::ivec2 usablePosition{0, 0}; // the bounds without the taskbar, the menu bar and the dock
  glm::uvec2 usableSize{0, 0};
  bool primary{false};
};

// A window's rectangle in desktop coordinates: the client area, without the title bar.
struct WindowRect {
  glm::ivec2 position{0, 0};
  glm::uvec2 size{0, 0};

  friend bool operator==(const WindowRect &, const WindowRect &) = default;
};

struct WindowDesc {
  std::string title{"Sonnet"};
  glm::uvec2 size{1280, 720}; // logical size; the pixel size may be larger on high-density displays
  // Where the window's client area opens, in desktop coordinates; the system chooses when empty.
  // Wayland ignores it (Platform::canPositionWindows).
  std::optional<glm::ivec2> position{};
  // Opens maximized. `size` and `position` are then the rectangle a restore returns to.
  bool maximized{false};
  bool resizable{true};
  bool hidden{false};
  // The display rate the window asks for, in frames per second; 0 leaves it to the system. Only
  // Android honours it, where a FIFO swapchain then paces the frames to it (docs/platform.md).
  float frameRate{0.0F};
};

class IWindow {
public:
  virtual ~IWindow() = default;

  [[nodiscard]] virtual glm::uvec2 size() const = 0;
  [[nodiscard]] virtual glm::uvec2 pixelSize() const = 0;
  [[nodiscard]] virtual bool isMinimized() const = 0;
  [[nodiscard]] virtual bool isMaximized() const = 0;
  // The client area's top-left in desktop coordinates. Wayland does not tell, and reports 0, 0.
  [[nodiscard]] virtual glm::ivec2 position() const = 0;
  // The rectangle a restore returns to. SDL cannot report it while the window is maximized, so it
  // is tracked from the window's moves and resizes while it is not (docs/platform.md, "Window").
  [[nodiscard]] virtual WindowRect restoredRect() const = 0;
  // The display the window is mostly on; empty when SDL names none.
  [[nodiscard]] virtual std::optional<Display> display() const = 0;
  [[nodiscard]] virtual std::string_view title() const = 0;
  virtual void setTitle(std::string_view title) = 0;
  virtual void show() = 0;
  // Hides the cursor and reports mouse motion as deltas only, for fly-camera style control.
  // False when the platform cannot (SDL's X11 driver without XInput2, the offscreen driver).
  virtual bool setRelativeMouseMode(bool enabled) = 0;
  [[nodiscard]] virtual bool relativeMouseMode() const = 0;

  // The surface belongs to the caller, who destroys it with vkDestroySurfaceKHR before the window.
  // Throws core::Exception when the surface cannot be created.
  [[nodiscard]] virtual VkSurfaceKHR createVulkanSurface(VkInstance instance) const = 0;

  // The SDL window behind the interface, for Dear ImGui's SDL3 backend in `ui`. Nothing else
  // reaches SDL through it.
  [[nodiscard]] virtual SDL_Window *nativeHandle() const = 0;
};

} // namespace sonnet::platform
