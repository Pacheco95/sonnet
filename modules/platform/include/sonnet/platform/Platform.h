#pragma once

#include <sonnet/platform/Window.h>

#include <vulkan/vulkan_core.h>

#include <filesystem>
#include <memory>
#include <span>
#include <string_view>

namespace sonnet::platform {

struct PlatformDesc {
  // Uses SDL's offscreen video driver: windows exist, nothing is displayed, Vulkan still loads.
  // For tests and CI.
  bool headless{false};
};

// Owns the SDL video subsystem. Exactly one instance exists while the application runs; the
// entry point creates it before the application and destroys it after.
class Platform {
public:
  explicit Platform(const PlatformDesc &desc = {});
  ~Platform();
  Platform(const Platform &) = delete;
  Platform &operator=(const Platform &) = delete;

  [[nodiscard]] std::unique_ptr<IWindow> createWindow(const WindowDesc &desc);

  [[nodiscard]] bool isHeadless() const noexcept {
    return m_headless;
  }
  [[nodiscard]] std::string_view videoDriver() const;

  // Directory of the executable's data (next to the binary on desktop, the bundle on mobile).
  [[nodiscard]] std::filesystem::path basePath() const;
  // Per-user writable directory for preferences and saves, created if missing.
  [[nodiscard]] std::filesystem::path prefPath(std::string_view organisation, std::string_view application) const;

  // Both load the Vulkan library through SDL on first use so the whole process shares one loader
  // (ADR-0006). They throw core::Exception when no Vulkan loader is available.
  [[nodiscard]] std::span<const char *const> vulkanInstanceExtensions();
  [[nodiscard]] PFN_vkGetInstanceProcAddr vulkanGetInstanceProcAddr();

private:
  void loadVulkan();

  bool m_headless{false};
  bool m_vulkanLoaded{false};
};

} // namespace sonnet::platform
