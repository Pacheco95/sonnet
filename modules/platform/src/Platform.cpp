#include <sonnet/platform/Platform.h>

#include "SdlWindow.h"

#include <sonnet/core/Error.h>
#include <sonnet/core/Log.h>
#include <sonnet/core/Version.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <format>
#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace sonnet::platform {

namespace {

// Keeps the Vulkan loader mapped for the life of the process, whatever SDL does with it.
//
// SDL unloads the library when the video subsystem quits, and vk-bootstrap caches the loader's
// entry points in a table it initialises once and never refreshes. A second Platform in one
// process therefore hands vk-bootstrap a new loader, which it ignores, and the next device calls
// through pointers into the unmapped library. On macOS that is a crash; elsewhere it survives
// only when the library happens to be mapped at its old address (docs/platform.md).
//
// The reference is taken once and never released, so the library outlives every Platform.
void retainVulkanLibrary() {
  static const bool Retained = [] {
    const SDL_FunctionPointer loader = SDL_Vulkan_GetVkGetInstanceProcAddr();
    if (loader == nullptr) {
      return false;
    }
    // Conditionally supported by the standard, required to work by POSIX and by Windows, which is
    // where the loader comes from in the first place.
    void *const address = reinterpret_cast<void *>(loader);
#if defined(_WIN32)
    HMODULE module = nullptr;
    // PIN keeps the module loaded until the process exits, so the handle needs no release.
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(address), &module) == 0) {
      SONNET_LOG_WARN("could not pin the Vulkan loader: GetModuleHandleEx failed ({})", GetLastError());
      return false;
    }
    return true;
#else
    Dl_info info{};
    if (dladdr(address, &info) == 0 || info.dli_fname == nullptr) {
      SONNET_LOG_WARN("could not find the Vulkan loader's library to keep it mapped");
      return false;
    }
    // NOLOAD raises the reference count of the library already open rather than opening another.
    void *library = dlopen(info.dli_fname, RTLD_LAZY | RTLD_LOCAL | RTLD_NOLOAD);
    if (library == nullptr) {
      library = dlopen(info.dli_fname, RTLD_LAZY | RTLD_LOCAL);
    }
    if (library == nullptr) {
      SONNET_LOG_WARN("could not keep the Vulkan loader mapped: {}", dlerror());
      return false;
    }
    SONNET_LOG_DEBUG("Vulkan loader \"{}\" kept mapped for the process", info.dli_fname);
    return true;
#endif
  }();
  static_cast<void>(Retained);
}

} // namespace

Platform::Platform(const PlatformDesc &desc) : m_headless(desc.headless) {
  if (m_headless) {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    // Headless means no UI at all: an SDL assertion must not open its dialog and wait.
    SDL_SetHint(SDL_HINT_ASSERT, "abort");
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
  retainVulkanLibrary();
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
