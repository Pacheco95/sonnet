#include <sonnet/platform/Platform.h>
#include <sonnet/platform/Window.h>

#include "RestoredRect.h"

#include <sonnet/core/Error.h>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>

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

TEST_CASE("the restored rectangle follows moves and resizes until the window is maximized", "[platform][window]") {
  using namespace sonnet::platform;
  RestoredRect tracked{{{100, 80}, {1600, 900}}};
  tracked.moved({200, 120}, false);
  tracked.resized({1200, 700}, false);
  REQUIRE(tracked.rect() == WindowRect{{200, 120}, {1200, 700}});

  // Maximizing moves and resizes the window to the display; the rectangle to restore stays.
  tracked.moved({0, 0}, true);
  tracked.resized({2560, 1400}, true);
  REQUIRE(tracked.rect() == WindowRect{{200, 120}, {1200, 700}});

  tracked.moved({300, 150}, false); // restored, then dragged
  REQUIRE(tracked.rect() == WindowRect{{300, 150}, {1200, 700}});
}

TEST_CASE("a window opens at the requested position and reports it", "[platform][window]") {
  sonnet::platform::Platform platform{{.headless = true}};
  requireVulkan(platform);
  const auto window = platform.createWindow({.title = "placed", .size = {400, 300}, .position = glm::ivec2{120, 90}});
  SDL_PumpEvents();
  REQUIRE(window->size() == glm::uvec2{400, 300});
  REQUIRE(window->position() == glm::ivec2{120, 90});
  REQUIRE(window->restoredRect() == sonnet::platform::WindowRect{{120, 90}, {400, 300}});
  REQUIRE(!window->isMaximized());
}

TEST_CASE("a moved window updates its restored rectangle", "[platform][window]") {
  sonnet::platform::Platform platform{{.headless = true}};
  requireVulkan(platform);
  const auto window = platform.createWindow({.title = "moved", .size = {400, 300}, .position = glm::ivec2{10, 10}});
  // The offscreen driver cannot move a window, so the window manager's reports are pushed by hand.
  const auto push = [&](SDL_EventType type, int a, int b) {
    SDL_Event event{};
    event.type = type;
    event.window.windowID = SDL_GetWindowID(window->nativeHandle());
    event.window.data1 = a;
    event.window.data2 = b;
    SDL_PushEvent(&event);
  };
  push(SDL_EVENT_WINDOW_MOVED, 250, 160);
  push(SDL_EVENT_WINDOW_RESIZED, 500, 320);
  REQUIRE(window->restoredRect() == sonnet::platform::WindowRect{{250, 160}, {500, 320}});
}

TEST_CASE("the platform lists its displays and the window names the one it is on", "[platform][window]") {
  sonnet::platform::Platform platform{{.headless = true}};
  requireVulkan(platform);
  const auto displays = platform.displays();
  REQUIRE(!displays.empty());
  for (const auto &display : displays) {
    REQUIRE(display.size.x > 0);
    REQUIRE(display.size.y > 0);
    REQUIRE(display.usableSize.x > 0);
    REQUIRE(display.usableSize.y > 0);
  }
  REQUIRE(displays.front().primary);
  const auto window = platform.createWindow({.title = "on a display"});
  const auto display = window->display();
  REQUIRE(display.has_value());
  REQUIRE(std::ranges::any_of(displays, [&](const auto &listed) { return listed.id == display->id; }));
}
