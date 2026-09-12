#include <sonnet/platform/Platform.h>

#include "SdlWindow.h"

#include <sonnet/core/Error.h>
#include <sonnet/core/Log.h>
#include <sonnet/core/Version.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <format>
#include <string>

namespace sonnet::platform {

Platform::Platform(const PlatformDesc &desc) : m_headless(desc.headless) {
  if (m_headless) {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
  }
  // Only the video subsystem for now; audio and gamepads join in their milestones. Subsystems the
  // engine does not use stay off (ADR-0002).
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    throw core::Exception{std::format("SDL_Init failed: {}", SDL_GetError()), core::ErrorCategory::Platform};
  }
  SONNET_LOG_INFO("SDL {}.{}.{} initialised, video driver \"{}\"{}", SDL_VERSIONNUM_MAJOR(SDL_GetVersion()),
                  SDL_VERSIONNUM_MINOR(SDL_GetVersion()), SDL_VERSIONNUM_MICRO(SDL_GetVersion()), videoDriver(),
                  m_headless ? " (headless)" : "");
}

Platform::~Platform() {
  if (m_vulkanLoaded) {
    SDL_Vulkan_UnloadLibrary();
  }
  SDL_Quit();
}

std::unique_ptr<IWindow> Platform::createWindow(const WindowDesc &desc) {
  return std::make_unique<SdlWindow>(desc);
}

std::string_view Platform::videoDriver() const {
  const char *driver = SDL_GetCurrentVideoDriver();
  return driver != nullptr ? driver : "";
}

std::filesystem::path Platform::basePath() const {
  const char *base = SDL_GetBasePath();
  if (base == nullptr) {
    throw core::Exception{std::format("SDL_GetBasePath failed: {}", SDL_GetError()), core::ErrorCategory::Platform};
  }
  return std::filesystem::path{base};
}

std::filesystem::path Platform::prefPath(std::string_view organisation, std::string_view application) const {
  char *pref = SDL_GetPrefPath(std::string{organisation}.c_str(), std::string{application}.c_str());
  if (pref == nullptr) {
    throw core::Exception{std::format("SDL_GetPrefPath failed: {}", SDL_GetError()), core::ErrorCategory::Platform};
  }
  std::filesystem::path path{pref};
  SDL_free(pref);
  return path;
}

void Platform::loadVulkan() {
  if (m_vulkanLoaded) {
    return;
  }
  if (!SDL_Vulkan_LoadLibrary(nullptr)) {
    throw core::Exception{std::format("Vulkan loader not available: {}", SDL_GetError()),
                          core::ErrorCategory::Platform};
  }
  m_vulkanLoaded = true;
}

std::span<const char *const> Platform::vulkanInstanceExtensions() {
  loadVulkan();
  Uint32 count = 0;
  const char *const *names = SDL_Vulkan_GetInstanceExtensions(&count);
  if (names == nullptr) {
    throw core::Exception{std::format("SDL_Vulkan_GetInstanceExtensions failed: {}", SDL_GetError()),
                          core::ErrorCategory::Platform};
  }
  return {names, count};
}

PFN_vkGetInstanceProcAddr Platform::vulkanGetInstanceProcAddr() {
  loadVulkan();
  const SDL_FunctionPointer fn = SDL_Vulkan_GetVkGetInstanceProcAddr();
  if (fn == nullptr) {
    throw core::Exception{std::format("SDL_Vulkan_GetVkGetInstanceProcAddr failed: {}", SDL_GetError()),
                          core::ErrorCategory::Platform};
  }
  return reinterpret_cast<PFN_vkGetInstanceProcAddr>(fn);
}

} // namespace sonnet::platform
