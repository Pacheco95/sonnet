#include <sonnet/platform/Platform.h>

#include <catch2/catch_test_macros.hpp>

#include <dlfcn.h>

#include <cstdlib>
#include <filesystem>
#include <string_view>

TEST_CASE("the editor keeps a Vulkan loader outside vcpkg mapped", "[editor][loader]") {
  // The TSan preset disables every Vulkan driver so GPU work does not enter uninstrumented code.
  if (const char *driverFiles = std::getenv("VK_DRIVER_FILES");
      driverFiles != nullptr && std::string_view{driverFiles} == "/dev/null") {
    SKIP("Vulkan drivers are disabled by this preset");
  }
  PFN_vkGetInstanceProcAddr entry = nullptr;
  {
    sonnet::platform::Platform platform{{.headless = true}};
    entry = platform.vulkanGetInstanceProcAddr();
    REQUIRE(entry != nullptr);
  }
  // Platform retains SDL's loader after shutdown because vk-bootstrap caches its entry points.
  Dl_info library{};
  REQUIRE(dladdr(reinterpret_cast<void *>(entry), &library) != 0);
  REQUIRE(library.dli_fname != nullptr);
  const auto path = std::filesystem::canonical(library.dli_fname);
  INFO("retained Vulkan loader: " << path);
  for (const auto &part : path) {
    REQUIRE(part != "vcpkg_installed");
  }
}
