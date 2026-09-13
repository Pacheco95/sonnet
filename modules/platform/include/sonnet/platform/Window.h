#pragma once

#include <sonnet/core/Math.h>

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <string>
#include <string_view>

// SDL is the only implementation (ADR-0002) and Dear ImGui's SDL3 backend needs the window it
// wraps, so the interface hands the SDL handle out; declaring it here keeps SDL's headers out.
struct SDL_Window;

namespace sonnet::platform {

struct WindowDesc {
  std::string title{"Sonnet"};
  glm::uvec2 size{1280, 720}; // logical size; the pixel size may be larger on high-density displays
  bool resizable{true};
  bool hidden{false};
};

class IWindow {
public:
  virtual ~IWindow() = default;

  [[nodiscard]] virtual glm::uvec2 size() const = 0;
  [[nodiscard]] virtual glm::uvec2 pixelSize() const = 0;
  [[nodiscard]] virtual bool isMinimized() const = 0;
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
