#include "SdlWindow.h"

#include <sonnet/core/Error.h>
#include <sonnet/core/Log.h>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_vulkan.h>

#include <format>

namespace sonnet::platform {

SdlWindow::SdlWindow(const WindowDesc &desc) : m_title(desc.title) {
  SDL_WindowFlags flags = SDL_WINDOW_VULKAN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
  if (desc.resizable) {
    flags |= SDL_WINDOW_RESIZABLE;
  }
  if (desc.hidden) {
    flags |= SDL_WINDOW_HIDDEN;
  }
  m_window = SDL_CreateWindow(m_title.c_str(), static_cast<int>(desc.size.x), static_cast<int>(desc.size.y), flags);
  if (m_window == nullptr) {
    throw core::Exception{std::format("SDL_CreateWindow failed: {}", SDL_GetError()), core::ErrorCategory::Platform};
  }
  const glm::uvec2 pixels = pixelSize();
  SONNET_LOG_DEBUG("window \"{}\" created: {}x{} logical, {}x{} pixels", m_title, desc.size.x, desc.size.y, pixels.x,
                   pixels.y);
}

SdlWindow::~SdlWindow() {
  SDL_DestroyWindow(m_window);
}

glm::uvec2 SdlWindow::size() const {
  int width = 0;
  int height = 0;
  SDL_GetWindowSize(m_window, &width, &height);
  return {static_cast<unsigned>(width), static_cast<unsigned>(height)};
}

glm::uvec2 SdlWindow::pixelSize() const {
  int width = 0;
  int height = 0;
  SDL_GetWindowSizeInPixels(m_window, &width, &height);
  return {static_cast<unsigned>(width), static_cast<unsigned>(height)};
}

bool SdlWindow::isMinimized() const {
  return (SDL_GetWindowFlags(m_window) & SDL_WINDOW_MINIMIZED) != 0;
}

std::string_view SdlWindow::title() const {
  return m_title;
}

void SdlWindow::setTitle(std::string_view title) {
  m_title.assign(title);
  SDL_SetWindowTitle(m_window, m_title.c_str());
}

void SdlWindow::show() {
  SDL_ShowWindow(m_window);
}

bool SdlWindow::setRelativeMouseMode(bool enabled) {
  if (!SDL_SetWindowRelativeMouseMode(m_window, enabled)) {
    SONNET_LOG_WARN("SDL_SetWindowRelativeMouseMode failed: {}", SDL_GetError());
    return false;
  }
  return true;
}

bool SdlWindow::relativeMouseMode() const {
  return SDL_GetWindowRelativeMouseMode(m_window);
}

VkSurfaceKHR SdlWindow::createVulkanSurface(VkInstance instance) const {
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  if (!SDL_Vulkan_CreateSurface(m_window, instance, nullptr, &surface)) {
    throw core::Exception{std::format("SDL_Vulkan_CreateSurface failed: {}", SDL_GetError()),
                          core::ErrorCategory::Platform};
  }
  return surface;
}

} // namespace sonnet::platform
