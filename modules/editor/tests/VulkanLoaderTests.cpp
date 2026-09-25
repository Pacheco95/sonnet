#include <sonnet/platform/Platform.h>

#include <catch2/catch_test_macros.hpp>

#include <dlfcn.h>

#include <filesystem>

TEST_CASE("the editor keeps a Vulkan loader outside vcpkg mapped", "[editor][loader]") {
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
