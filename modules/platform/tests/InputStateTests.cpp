#include <sonnet/platform/InputState.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

using namespace sonnet::platform;

TEST_CASE("keys and buttons are held between their press and release", "[platform][input]") {
  InputState input;
  input.beginFrame();
  input.handle(KeyPressed{.key = Key::W, .modifiers = Modifiers::None, .repeat = false});
  input.handle(MouseButtonPressed{.button = MouseButton::Left, .position = {10.0f, 20.0f}, .clicks = 1});
  REQUIRE(input.keyDown(Key::W));
  REQUIRE(input.keyPressed(Key::W));
  REQUIRE_FALSE(input.keyDown(Key::S));
  REQUIRE(input.mouseDown(MouseButton::Left));
  REQUIRE(input.mousePressed(MouseButton::Left));
  REQUIRE(input.mousePosition() == glm::vec2{10.0f, 20.0f});

  // The edge lasts one frame; the state lasts until the release.
  input.beginFrame();
  input.handle(KeyPressed{.key = Key::W, .modifiers = Modifiers::None, .repeat = true});
  REQUIRE(input.keyDown(Key::W));
  REQUIRE_FALSE(input.keyPressed(Key::W));
  REQUIRE_FALSE(input.mousePressed(MouseButton::Left));

  input.beginFrame();
  input.handle(KeyReleased{.key = Key::W, .modifiers = Modifiers::None});
  input.handle(MouseButtonReleased{.button = MouseButton::Left, .position = {12.0f, 20.0f}});
  REQUIRE_FALSE(input.keyDown(Key::W));
  REQUIRE(input.keyReleased(Key::W));
  REQUIRE(input.mouseReleased(MouseButton::Left));
  REQUIRE_FALSE(input.keyDown(Key::Unknown));
  REQUIRE_FALSE(input.keyDown(Key::Count));
}

TEST_CASE("motion and wheel accumulate over a frame", "[platform][input]") {
  InputState input;
  input.beginFrame();
  input.handle(MouseMoved{.position = {5.0f, 5.0f}, .delta = {2.0f, 1.0f}});
  input.handle(MouseMoved{.position = {8.0f, 4.0f}, .delta = {3.0f, -1.0f}});
  input.handle(MouseWheel{.delta = {0.0f, 1.0f}});
  REQUIRE(input.mousePosition() == glm::vec2{8.0f, 4.0f});
  REQUIRE(input.mouseDelta() == glm::vec2{5.0f, 0.0f});
  REQUIRE(input.wheel() == glm::vec2{0.0f, 1.0f});
  input.beginFrame();
  REQUIRE(input.mouseDelta() == glm::vec2{0.0f, 0.0f});
  REQUIRE(input.wheel() == glm::vec2{0.0f, 0.0f});
  REQUIRE(input.mousePosition() == glm::vec2{8.0f, 4.0f});
}

TEST_CASE("losing the focus releases everything held", "[platform][input]") {
  InputState input;
  input.beginFrame();
  input.handle(KeyPressed{.key = Key::Space, .modifiers = Modifiers::None, .repeat = false});
  input.handle(MouseButtonPressed{.button = MouseButton::Right, .position = {}, .clicks = 1});
  input.beginFrame();
  input.handle(WindowFocusChanged{.focused = false});
  REQUIRE_FALSE(input.keyDown(Key::Space));
  REQUIRE(input.keyReleased(Key::Space));
  REQUIRE_FALSE(input.mouseDown(MouseButton::Right));
  REQUIRE(input.mouseReleased(MouseButton::Right));
}

TEST_CASE("touches appear, move by a per-frame motion and end", "[platform][input]") {
  InputState input;
  input.beginFrame();
  REQUIRE(input.touches().empty());
  input.handle(TouchDown{.id = 4, .position = {10.0f, 20.0f}});
  input.handle(TouchDown{.id = 9, .position = {100.0f, 50.0f}});
  REQUIRE(input.touches().size() == 2);
  REQUIRE(input.touches()[0].id == 4);
  REQUIRE(input.touches()[0].position == glm::vec2{10.0f, 20.0f});
  REQUIRE(input.touches()[0].delta == glm::vec2{0.0f, 0.0f});
  REQUIRE(input.touches()[1].id == 9);

  input.handle(TouchMotion{.id = 4, .position = {12.0f, 21.0f}, .delta = {2.0f, 1.0f}});
  input.handle(TouchMotion{.id = 4, .position = {15.0f, 21.0f}, .delta = {3.0f, 0.0f}});
  REQUIRE(input.touches()[0].position == glm::vec2{15.0f, 21.0f});
  REQUIRE(input.touches()[0].delta == glm::vec2{5.0f, 1.0f});
  REQUIRE(input.touches()[1].delta == glm::vec2{0.0f, 0.0f});

  // The motion is this frame's; the position stays.
  input.beginFrame();
  REQUIRE(input.touches()[0].delta == glm::vec2{0.0f, 0.0f});
  REQUIRE(input.touches()[0].position == glm::vec2{15.0f, 21.0f});

  // The first finger lifts; the second keeps its place as the only one.
  input.handle(TouchUp{.id = 4, .position = {15.0f, 21.0f}});
  REQUIRE(input.touches().size() == 1);
  REQUIRE(input.touches()[0].id == 9);
  // A finger that went down before anyone listened is ignored until it lifts.
  input.handle(TouchMotion{.id = 5, .position = {1.0f, 1.0f}, .delta = {1.0f, 1.0f}});
  input.handle(TouchUp{.id = 5, .position = {1.0f, 1.0f}});
  REQUIRE(input.touches().size() == 1);
  input.handle(TouchUp{.id = 9, .position = {100.0f, 50.0f}});
  REQUIRE(input.touches().empty());
}

TEST_CASE("touches end with the focus and past the most fingers kept", "[platform][input]") {
  InputState input;
  for (std::uint64_t id = 0; id <= InputState::MaxTouches; ++id) {
    input.handle(TouchDown{.id = id, .position = {}});
  }
  REQUIRE(input.touches().size() == InputState::MaxTouches);
  input.handle(WindowFocusChanged{.focused = false});
  REQUIRE(input.touches().empty());
  input.handle(TouchDown{.id = 1, .position = {}});
  input.releaseAll();
  REQUIRE(input.touches().empty());
}

TEST_CASE("keys and buttons have names scripts can use", "[platform][input]") {
  REQUIRE(toString(Key::A) == "A");
  REQUIRE(toString(Key::Space) == "Space");
  REQUIRE(toString(Key::Menu) == "Menu");
  REQUIRE(toString(Key::Unknown).empty());
  REQUIRE(keyFromName("LeftShift") == Key::LeftShift);
  REQUIRE(keyFromName("Digit7") == Key::Digit7);
  REQUIRE_FALSE(keyFromName("Nope").has_value());
  REQUIRE_FALSE(keyFromName("").has_value());
  REQUIRE(toString(MouseButton::Right) == "Right");
  REQUIRE(mouseButtonFromName("X2") == MouseButton::X2);
  REQUIRE_FALSE(mouseButtonFromName("Up").has_value());
  for (auto i = 1; i < static_cast<int>(Key::Count); ++i) {
    const auto key = static_cast<Key>(i);
    REQUIRE(keyFromName(toString(key)) == key);
  }
}
