#include <sonnet/platform/Platform.h>
#include <sonnet/platform/Window.h>

#include <sonnet/core/Error.h>

#include <catch2/catch_test_macros.hpp>

namespace {

// Windows are created Vulkan-capable, which needs the Vulkan library; machines without one
// (CI runners for Windows and macOS) cannot create windows at all.
void requireVulkan(sonnet::platform::Platform &platform) {
  try {
    static_cast<void>(platform.vulkanGetInstanceProcAddr());
  } catch (const sonnet::core::Exception &e) {
    SKIP("no Vulkan loader on this machine: " << e.what());
  }
}

} // namespace

TEST_CASE("window reports the requested size", "[platform][window]") {
  sonnet::platform::Platform platform{{.headless = true}};
  requireVulkan(platform);
  const auto window = platform.createWindow({.title = "test", .size = {640, 360}});
  REQUIRE(window->size() == glm::uvec2{640, 360});
  REQUIRE(window->pixelSize().x >= 640);
  REQUIRE(window->pixelSize().y >= 360);
  REQUIRE(!window->isMinimized());
}

TEST_CASE("window title can be changed", "[platform][window]") {
  sonnet::platform::Platform platform{{.headless = true}};
  requireVulkan(platform);
  const auto window = platform.createWindow({.title = "before"});
  REQUIRE(window->title() == "before");
  window->setTitle("after");
  REQUIRE(window->title() == "after");
}

TEST_CASE("several windows can coexist", "[platform][window]") {
  sonnet::platform::Platform platform{{.headless = true}};
  requireVulkan(platform);
  const auto a = platform.createWindow({.title = "a", .size = {100, 100}});
  const auto b = platform.createWindow({.title = "b", .size = {200, 200}, .hidden = true});
  REQUIRE(a->size() == glm::uvec2{100, 100});
  REQUIRE(b->size() == glm::uvec2{200, 200});
}
