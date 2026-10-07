#include <sonnet/editor/WindowState.h>

#include <sonnet/platform/Platform.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <vector>

using namespace sonnet;
using platform::Display;
using platform::WindowRect;

namespace {

std::filesystem::path scratch(const char *name) {
  const std::filesystem::path path = std::filesystem::temp_directory_path() / "sonnet_editor_tests" / name;
  std::filesystem::remove_all(path);
  std::filesystem::create_directories(path.parent_path());
  return path;
}

// A laptop panel at the origin, with the dock taking 40 pixels, and a 4K monitor to its right.
Display laptop() {
  return {.id = 1,
          .name = "Laptop",
          .position = {0, 0},
          .size = {1920, 1080},
          .usablePosition = {0, 40},
          .usableSize = {1920, 1040},
          .primary = true};
}
Display monitor() {
  return {.id = 2,
          .name = "Monitor",
          .position = {1920, 0},
          .size = {3840, 2160},
          .usablePosition = {1920, 0},
          .usableSize = {3840, 2160},
          .primary = false};
}

editor::WindowState onMonitor() {
  return editor::makeWindowState({{2220, 300}, {1600, 900}}, false, monitor());
}

} // namespace

TEST_CASE("a window state is saved and read back", "[editor][window]") {
  const std::filesystem::path directory = scratch("window_state");
  std::filesystem::create_directories(directory);
  const std::filesystem::path file = directory / "window.json";
  const editor::WindowState state = editor::makeWindowState({{2220, 300}, {1280, 800}}, true, monitor());
  REQUIRE(state.position == glm::ivec2{300, 300}); // relative to the display
  REQUIRE(state.save(file).has_value());

  const auto loaded = editor::WindowState::load(file);
  REQUIRE(loaded.has_value());
  REQUIRE(loaded->size == glm::uvec2{1280, 800});
  REQUIRE(loaded->position == glm::ivec2{300, 300});
  REQUIRE(loaded->maximized);
  REQUIRE(loaded->display == "Monitor");
  REQUIRE(loaded->displayPosition == glm::ivec2{1920, 0});
  REQUIRE(loaded->displaySize == glm::uvec2{3840, 2160});
}

TEST_CASE("a missing, corrupt or implausible window file is ignored", "[editor][window]") {
  const std::filesystem::path directory = scratch("window_state_bad");
  std::filesystem::create_directories(directory);
  REQUIRE(!editor::WindowState::load(directory / "missing.json").has_value());

  const auto write = [&](const char *name, const char *text) {
    std::ofstream{directory / name} << text;
    return editor::WindowState::load(directory / name);
  };
  REQUIRE(!write("garbage.json", "{ not json").has_value());
  REQUIRE(!write("array.json", "[1, 2]").has_value());
  REQUIRE(!write("empty.json", "{}").has_value());
  REQUIRE(!write("tiny.json", R"({"size":[3,3],"position":[0,0],"display":{"name":"A","position":[0,0],"size":[1,1]}})")
               .has_value());
  REQUIRE(
      !write("typed.json", R"({"size":["a",1],"position":[0,0],"display":{"name":"A","position":[0,0],"size":[1,1]}})")
           .has_value());
}

TEST_CASE("the window opens where it was, on the display it was on", "[editor][window]") {
  const std::vector<Display> displays{laptop(), monitor()};
  const editor::WindowPlacement placement = editor::placeWindow(onMonitor(), displays, true);
  REQUIRE(!placement.fallback);
  REQUIRE(placement.position == glm::ivec2{2220, 300});
  REQUIRE(placement.size == glm::uvec2{1600, 900});
  REQUIRE(!placement.maximized);

  platform::WindowDesc desc;
  placement.applyTo(desc);
  REQUIRE(desc.position == glm::ivec2{2220, 300});
  REQUIRE(desc.size == glm::uvec2{1600, 900});
}

TEST_CASE("the maximized flag survives the restore", "[editor][window]") {
  const std::vector<Display> displays{laptop(), monitor()};
  editor::WindowState saved = onMonitor();
  saved.maximized = true;
  const editor::WindowPlacement placement = editor::placeWindow(saved, displays, true);
  REQUIRE(placement.maximized);
  // The rectangle a restore returns to is still the saved one.
  REQUIRE(placement.position == glm::ivec2{2220, 300});
  REQUIRE(placement.size == glm::uvec2{1600, 900});
}

TEST_CASE("a monitor that moved in the arrangement is still found, with the window on it", "[editor][window]") {
  Display moved = monitor();
  moved.position = {-3840, 0}; // now to the left
  moved.usablePosition = {-3840, 0};
  const std::vector<Display> displays{laptop(), moved};
  const editor::WindowPlacement placement = editor::placeWindow(onMonitor(), displays, true);
  REQUIRE(!placement.fallback);
  REQUIRE(placement.position == glm::ivec2{-3840 + 300, 300});
}

TEST_CASE("a missing display falls back to the primary, centred", "[editor][window]") {
  const std::vector<Display> displays{laptop()};
  const editor::WindowPlacement placement = editor::placeWindow(onMonitor(), displays, true);
  REQUIRE(placement.fallback);
  REQUIRE(placement.size == glm::uvec2{1600, 900});
  // Centred in the usable area, below the dock.
  REQUIRE(placement.position == glm::ivec2{160, 40 + 70});
  REQUIRE(placement.state.display == "Laptop");
}

TEST_CASE("the fallback clamps the size to the primary's usable area and keeps the maximized flag",
          "[editor][window]") {
  editor::WindowState saved = editor::makeWindowState({{2000, 100}, {3600, 2000}}, true, monitor());
  const std::vector<Display> displays{laptop()};
  const editor::WindowPlacement placement = editor::placeWindow(saved, displays, true);
  REQUIRE(placement.fallback);
  REQUIRE(placement.size == glm::uvec2{1920, 1040});
  REQUIRE(placement.position == glm::ivec2{0, 40});
  REQUIRE(placement.maximized);
}

TEST_CASE("a rectangle that is off every display is clamped onto the primary", "[editor][window]") {
  // The monitor is still there, but at a lower resolution: the saved corner is past its edge.
  Display smaller = monitor();
  smaller.size = {1280, 720};
  smaller.usableSize = {1280, 720};
  const std::vector<Display> displays{laptop(), smaller};
  editor::WindowState saved = onMonitor();
  saved.position = {3000, 1800};
  const editor::WindowPlacement placement = editor::placeWindow(saved, displays, true);
  REQUIRE(placement.fallback);
  REQUIRE(placement.position.has_value());
  REQUIRE(placement.position->x >= 0);
  REQUIRE(placement.position->x + static_cast<int>(placement.size.x) <= 1920);
  REQUIRE(placement.state.display == "Laptop");
}

TEST_CASE("a window straddling two displays stays where it was", "[editor][window]") {
  const std::vector<Display> displays{laptop(), monitor()};
  const editor::WindowState saved = editor::makeWindowState({{1500, 200}, {1000, 700}}, false, laptop());
  const editor::WindowPlacement placement = editor::placeWindow(saved, displays, true);
  REQUIRE(!placement.fallback);
  REQUIRE(placement.position == glm::ivec2{1500, 200});
}

TEST_CASE("Wayland restores the size and the maximized flag only", "[editor][window]") {
  const std::vector<Display> displays{laptop(), monitor()};
  editor::WindowState saved = onMonitor();
  saved.maximized = true;
  const editor::WindowPlacement placement = editor::placeWindow(saved, displays, false);
  REQUIRE(!placement.position.has_value());
  REQUIRE(!placement.fallback);
  REQUIRE(placement.size == glm::uvec2{1600, 900});
  REQUIRE(placement.maximized);

  saved.size = {5000, 3000}; // larger than the display it was on
  REQUIRE(editor::placeWindow(saved, displays, false).size == glm::uvec2{3840, 2160});
}

TEST_CASE("without a saved state the window opens centred on the primary, clamped", "[editor][window]") {
  const std::vector<Display> displays{laptop()};
  const editor::WindowPlacement placement = editor::placeWindow(std::nullopt, displays, true);
  REQUIRE(!placement.fallback); // there was no choice to fall back from
  REQUIRE(placement.size == glm::uvec2{1600, 900});
  REQUIRE(placement.position == glm::ivec2{160, 110});
  REQUIRE(!placement.maximized);

  const std::vector<Display> none;
  const editor::WindowPlacement unknown = editor::placeWindow(std::nullopt, none, true);
  REQUIRE(!unknown.position.has_value());
  REQUIRE(unknown.size == glm::uvec2{1600, 900});
}

TEST_CASE("a fallback window left as it opened is the same state, and a moved one is not", "[editor][window]") {
  const std::vector<Display> displays{laptop()};
  const editor::WindowPlacement placement = editor::placeWindow(onMonitor(), displays, true);
  REQUIRE(placement.fallback);

  // The window manager nudges it by its title bar.
  editor::WindowState nudged = placement.state;
  nudged.position += glm::ivec2{0, 30};
  REQUIRE(placement.state.sameAs(nudged));

  editor::WindowState dragged = placement.state;
  dragged.position += glm::ivec2{400, 0};
  REQUIRE(!placement.state.sameAs(dragged));

  editor::WindowState resized = placement.state;
  resized.size.x -= 100;
  REQUIRE(!placement.state.sameAs(resized));

  editor::WindowState maximized = placement.state;
  maximized.maximized = true;
  REQUIRE(!placement.state.sameAs(maximized));
}

TEST_CASE("a window's state is captured from its restored rectangle while maximized", "[editor][window]") {
  sonnet::platform::Platform platform{{.headless = true}};
  try {
    static_cast<void>(platform.vulkanGetInstanceProcAddr());
  } catch (const core::Exception &e) {
    SKIP("no Vulkan loader on this machine: " << e.what());
  }
  const auto window = platform.createWindow({.title = "captured", .size = {640, 360}, .position = glm::ivec2{50, 60}});
  const auto state = editor::captureWindowState(*window);
  REQUIRE(state.has_value());
  REQUIRE(state->size == glm::uvec2{640, 360});
  REQUIRE(!state->maximized);
  const auto display = window->display();
  REQUIRE(display.has_value());
  REQUIRE(state->display == display->name);
  REQUIRE(state->position == glm::ivec2{50, 60} - display->position);
}
