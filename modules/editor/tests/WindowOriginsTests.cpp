#include <sonnet/editor/WindowOrigins.h>

#include <catch2/catch_test_macros.hpp>

using namespace sonnet;

TEST_CASE("a position is mapped through the origin of the window it arrived in", "[editor][input]") {
  editor::WindowOrigins origins;
  origins.setMain({100.0f, 50.0f});
  origins.set(2, {900.0f, 300.0f}); // the Game view, undocked to a window of its own
  const glm::vec2 image{910.0f, 330.0f};

  // The same point of the screen, reported by the window it is over.
  REQUIRE(origins.relativeToImage(2, {20.0f, 40.0f}, image) == glm::vec2{10.0f, 10.0f});
  REQUIRE(origins.relativeToImage(1, {810.0f, 280.0f}, image) == glm::vec2{0.0f, 0.0f}); // main, unknown id
  REQUIRE(origins.relativeToImage(0, {810.0f, 280.0f}, image) == glm::vec2{0.0f, 0.0f}); // unnamed
}

TEST_CASE("a window that closed is forgotten and falls back to the main one", "[editor][input]") {
  editor::WindowOrigins origins;
  origins.setMain({0.0f, 0.0f});
  origins.set(3, {500.0f, 500.0f});
  REQUIRE(origins.origin(3) == glm::vec2{500.0f, 500.0f});
  origins.clear();
  REQUIRE(origins.origin(3) == glm::vec2{0.0f, 0.0f});
}
