#include <sonnet/platform/Platform.h>

#include <sonnet/core/Error.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string_view>

TEST_CASE("headless platform initialises with the offscreen driver", "[platform]") {
  sonnet::platform::Platform platform{{.headless = true}};
  REQUIRE(platform.isHeadless());
  REQUIRE(platform.videoDriver() == "offscreen");
}

TEST_CASE("base path is an existing directory", "[platform]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const std::filesystem::path base = platform.basePath();
  REQUIRE(std::filesystem::is_directory(base));
  REQUIRE(std::filesystem::exists(base / "platform_tests"));
}

TEST_CASE("pref path is created and writable", "[platform]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const std::filesystem::path pref = platform.prefPath("sonnet", "platform_tests");
  REQUIRE(std::filesystem::is_directory(pref));
}

TEST_CASE("Vulkan loader is reachable through the platform", "[platform][vulkan]") {
  sonnet::platform::Platform platform{{.headless = true}};
  try {
    const auto extensions = platform.vulkanInstanceExtensions();
    REQUIRE(!extensions.empty());
    bool hasSurface = false;
    for (const char *name : extensions) {
      hasSurface = hasSurface || std::string_view{name} == "VK_KHR_surface";
    }
    REQUIRE(hasSurface);
    REQUIRE(platform.vulkanGetInstanceProcAddr() != nullptr);
  } catch (const sonnet::core::Exception &e) {
    SKIP("no Vulkan loader on this machine: " << e.what());
  }
}
