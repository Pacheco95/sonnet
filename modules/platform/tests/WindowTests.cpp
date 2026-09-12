#include <sonnet/platform/Platform.h>
#include <sonnet/platform/Window.h>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("window reports the requested size", "[platform][window]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto window = platform.createWindow({.title = "test", .size = {640, 360}});
  REQUIRE(window->size() == glm::uvec2{640, 360});
  REQUIRE(window->pixelSize().x >= 640);
  REQUIRE(window->pixelSize().y >= 360);
  REQUIRE(!window->isMinimized());
}

TEST_CASE("window title can be changed", "[platform][window]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto window = platform.createWindow({.title = "before"});
  REQUIRE(window->title() == "before");
  window->setTitle("after");
  REQUIRE(window->title() == "after");
}

TEST_CASE("several windows can coexist", "[platform][window]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto a = platform.createWindow({.title = "a", .size = {100, 100}});
  const auto b = platform.createWindow({.title = "b", .size = {200, 200}, .hidden = true});
  REQUIRE(a->size() == glm::uvec2{100, 100});
  REQUIRE(b->size() == glm::uvec2{200, 200});
}
