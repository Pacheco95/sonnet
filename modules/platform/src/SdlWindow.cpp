#include "SdlWindow.h"

#include "SdlDisplay.h"

#include <sonnet/core/Error.h>
#include <sonnet/core/Log.h>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_vulkan.h>

#if defined(__ANDROID__)
#include <android/native_window.h>
#endif

#include <format>

namespace sonnet::platform {

SdlWindow::SdlWindow(const WindowDesc &desc)
    : m_restored(WindowRect{desc.position.value_or(glm::ivec2{0, 0}), desc.size}), m_title(desc.title),
      m_frameRate(desc.frameRate) {
  SDL_WindowFlags flags = SDL_WINDOW_VULKAN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
  if (desc.resizable) {
    flags |= SDL_WINDOW_RESIZABLE;
  }
  if (desc.hidden) {
    flags |= SDL_WINDOW_HIDDEN;
  }
  const SDL_PropertiesID properties = SDL_CreateProperties();
  SDL_SetStringProperty(properties, SDL_PROP_WINDOW_CREATE_TITLE_STRING, m_title.c_str());
  SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, desc.size.x);
  SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, desc.size.y);
  SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER, static_cast<Sint64>(flags));
  SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_MAXIMIZED_BOOLEAN, desc.maximized);
  if (desc.position) {
    SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_X_NUMBER, desc.position->x);
    SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_Y_NUMBER, desc.position->y);
  }
  m_window = SDL_CreateWindowWithProperties(properties);
  SDL_DestroyProperties(properties);
  if (m_window == nullptr) {
    throw core::Exception{std::format("SDL_CreateWindow failed: {}", SDL_GetError()), core::ErrorCategory::Platform};
  }
  if (!desc.position) {
    m_restored.moved(position(), isMaximized());
  }
  SDL_AddEventWatch(&SdlWindow::watchEvent, this);
  const glm::uvec2 pixels = pixelSize();
  SONNET_LOG_DEBUG("window \"{}\" created: {}x{} logical, {}x{} pixels", m_title, desc.size.x, desc.size.y, pixels.x,
                   pixels.y);
}

bool SdlWindow::watchEvent(void *userdata, SDL_Event *event) {
  auto *self = static_cast<SdlWindow *>(userdata);
  if (event->type != SDL_EVENT_WINDOW_MOVED && event->type != SDL_EVENT_WINDOW_RESIZED) {
    return true;
  }
  if (event->window.windowID != SDL_GetWindowID(self->m_window)) {
    return true;
  }
  const SDL_WindowFlags flags = SDL_GetWindowFlags(self->m_window);
  const bool frozen = (flags & (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_MINIMIZED | SDL_WINDOW_FULLSCREEN)) != 0;
  if (event->type == SDL_EVENT_WINDOW_MOVED) {
    self->m_restored.moved({event->window.data1, event->window.data2}, frozen);
  } else {
    self->m_restored.resized({static_cast<unsigned>(event->window.data1), static_cast<unsigned>(event->window.data2)},
                             frozen);
  }
  return true;
}

SdlWindow::~SdlWindow() {
  SDL_RemoveEventWatch(&SdlWindow::watchEvent, this);
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

bool SdlWindow::isMaximized() const {
  return (SDL_GetWindowFlags(m_window) & SDL_WINDOW_MAXIMIZED) != 0;
}

glm::ivec2 SdlWindow::position() const {
  int x = 0;
  int y = 0;
  SDL_GetWindowPosition(m_window, &x, &y);
  return {x, y};
}

WindowRect SdlWindow::restoredRect() const {
  return m_restored.rect();
}

std::optional<Display> SdlWindow::display() const {
  return displayFromSdl(SDL_GetDisplayForWindow(m_window));
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
#if defined(__ANDROID__)
  // Android replaces the ANativeWindow on every resume, and the swapchain creates a surface for
  // each one, so the rate is asked for here rather than once with the window.
  if (m_frameRate > 0.0F) {
    auto *nativeWindow = static_cast<ANativeWindow *>(
        SDL_GetPointerProperty(SDL_GetWindowProperties(m_window), SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr));
    if (nativeWindow == nullptr) {
      SONNET_LOG_WARN("no ANativeWindow to ask for {} fps", m_frameRate);
    } else if (const int result = ANativeWindow_setFrameRateWithChangeStrategy(
                   nativeWindow, m_frameRate, ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_DEFAULT,
                   ANATIVEWINDOW_CHANGE_FRAME_RATE_ONLY_IF_SEAMLESS);
               result != 0) {
      SONNET_LOG_WARN("ANativeWindow_setFrameRateWithChangeStrategy({} fps) failed: {}", m_frameRate, result);
    } else {
      SONNET_LOG_DEBUG("window asks the display for {} fps", m_frameRate);
    }
  }
#endif
  return surface;
}

} // namespace sonnet::platform
