#include <sonnet/platform/InputState.h>

#include <catch2/catch_approx.hpp>
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

TEST_CASE("gamepad buttons are held between their press and release, with one-frame edges",
          "[platform][input][gamepad]") {
  InputState input;
  REQUIRE_FALSE(input.gamepadConnected());
  input.handle(GamepadConnected{.name = "Pad"});
  REQUIRE(input.gamepadConnected());
  input.beginFrame();
  input.handle(GamepadButtonPressed{.button = GamepadButton::South});
  REQUIRE(input.gamepadDown(GamepadButton::South));
  REQUIRE(input.gamepadPressed(GamepadButton::South));
  REQUIRE_FALSE(input.gamepadDown(GamepadButton::East));
  input.beginFrame();
  // A press of a button already held is not another edge.
  input.handle(GamepadButtonPressed{.button = GamepadButton::South});
  REQUIRE(input.gamepadDown(GamepadButton::South));
  REQUIRE_FALSE(input.gamepadPressed(GamepadButton::South));
  input.beginFrame();
  input.handle(GamepadButtonReleased{.button = GamepadButton::South});
  input.handle(GamepadButtonReleased{.button = GamepadButton::North}); // never seen going down
  REQUIRE_FALSE(input.gamepadDown(GamepadButton::South));
  REQUIRE(input.gamepadReleased(GamepadButton::South));
  REQUIRE_FALSE(input.gamepadReleased(GamepadButton::North));
  REQUIRE_FALSE(input.gamepadDown(GamepadButton::Count));
}

TEST_CASE("the sticks have a radial dead zone that keeps a partial push and its direction",
          "[platform][input][gamepad]") {
  InputState input;
  const auto stick = [&](float x, float y) {
    input.handle(GamepadAxisMoved{.axis = GamepadAxis::LeftX, .value = x});
    input.handle(GamepadAxisMoved{.axis = GamepadAxis::LeftY, .value = y});
    return input.leftStick();
  };
  // Inside the dead zone, on either axis or both: centred.
  REQUIRE(stick(0.2f, 0.0f) == glm::vec2{0.0f, 0.0f});
  REQUIRE(stick(0.16f, 0.16f) == glm::vec2{0.0f, 0.0f});
  // Radial, not per axis: a diagonal whose components are each inside the dead zone counts.
  CHECK(glm::length(stick(0.2f, 0.2f)) > 0.0f);
  // Halfway between the dead zone and full is half a push, in the same direction.
  const float half = (InputState::StickDeadZone + InputState::StickFull) / 2.0f;
  const glm::vec2 partial = stick(half * 0.6f, half * 0.8f);
  CHECK(glm::length(partial) == Catch::Approx(0.5f).margin(1e-4f));
  CHECK(partial.x / partial.y == Catch::Approx(0.75f).margin(1e-4f));
  // Full and beyond, a corner included: no longer than 1.
  CHECK(glm::length(stick(0.0f, 1.0f)) == Catch::Approx(1.0f));
  CHECK(glm::length(stick(1.0f, 1.0f)) == Catch::Approx(1.0f));
  CHECK(stick(0.0f, -1.0f).y == Catch::Approx(-1.0f));
  // The right stick is its own.
  REQUIRE(input.rightStick() == glm::vec2{0.0f, 0.0f});
  input.handle(GamepadAxisMoved{.axis = GamepadAxis::RightX, .value = 1.0f});
  CHECK(input.rightStick().x == Catch::Approx(1.0f));
  CHECK(input.gamepadAxis(GamepadAxis::RightX) == 1.0f);
}

TEST_CASE("the triggers read from let go to pulled past their dead zone", "[platform][input][gamepad]") {
  InputState input;
  input.handle(GamepadAxisMoved{.axis = GamepadAxis::RightTrigger, .value = 0.1f});
  CHECK(input.rightTrigger() == 0.0f);
  CHECK(input.gamepadAxis(GamepadAxis::RightTrigger) == 0.1f); // the raw value stays readable
  const float half = (InputState::TriggerDeadZone + InputState::TriggerFull) / 2.0f;
  input.handle(GamepadAxisMoved{.axis = GamepadAxis::RightTrigger, .value = half});
  CHECK(input.rightTrigger() == Catch::Approx(0.5f).margin(1e-4f));
  input.handle(GamepadAxisMoved{.axis = GamepadAxis::LeftTrigger, .value = 1.0f});
  CHECK(input.leftTrigger() == 1.0f);
}

TEST_CASE("a lost focus or gamepad lets go of its buttons and centres its axes", "[platform][input][gamepad]") {
  InputState input;
  const auto hold = [&] {
    input.beginFrame();
    input.handle(GamepadButtonPressed{.button = GamepadButton::East});
    input.handle(GamepadAxisMoved{.axis = GamepadAxis::LeftX, .value = 1.0f});
    input.handle(GamepadAxisMoved{.axis = GamepadAxis::LeftTrigger, .value = 1.0f});
    input.beginFrame();
  };
  input.handle(GamepadConnected{.name = "Pad"});
  hold();
  input.handle(WindowFocusChanged{.focused = false});
  CHECK_FALSE(input.gamepadDown(GamepadButton::East));
  CHECK(input.gamepadReleased(GamepadButton::East));
  CHECK(input.leftStick() == glm::vec2{0.0f, 0.0f});
  CHECK(input.leftTrigger() == 0.0f);
  CHECK(input.gamepadConnected()); // the focus does not unplug it

  hold();
  input.handle(GamepadConnected{.name = "Another"}); // replacing the one that went
  CHECK(input.gamepadReleased(GamepadButton::East));
  CHECK(input.leftStick() == glm::vec2{0.0f, 0.0f});
  CHECK(input.gamepadConnected());

  hold();
  input.handle(GamepadDisconnected{});
  CHECK_FALSE(input.gamepadConnected());
  CHECK(input.gamepadReleased(GamepadButton::East));
  CHECK(input.leftStick() == glm::vec2{0.0f, 0.0f});
}

TEST_CASE("a reset forgets everything but whether a gamepad is connected", "[platform][input][gamepad]") {
  InputState input;
  input.handle(GamepadConnected{.name = "Pad"});
  input.handle(KeyPressed{.key = Key::W, .modifiers = Modifiers::None, .repeat = false});
  input.handle(GamepadButtonPressed{.button = GamepadButton::South});
  input.setMouseLocked(true);
  input.reset();
  CHECK(input.gamepadConnected());
  CHECK_FALSE(input.keyDown(Key::W));
  CHECK_FALSE(input.keyPressed(Key::W));
  CHECK_FALSE(input.gamepadDown(GamepadButton::South));
  CHECK_FALSE(input.gamepadReleased(GamepadButton::South));
  CHECK_FALSE(input.mouseLocked());
}

TEST_CASE("gamepad buttons have names scripts can use", "[platform][input][gamepad]") {
  REQUIRE(toString(GamepadButton::South) == "South");
  REQUIRE(toString(GamepadButton::DPadRight) == "DPadRight");
  REQUIRE(toString(GamepadButton::Count).empty());
  REQUIRE(gamepadButtonFromName("RightShoulder") == GamepadButton::RightShoulder);
  REQUIRE_FALSE(gamepadButtonFromName("A").has_value());
  for (auto i = 0; i < static_cast<int>(GamepadButton::Count); ++i) {
    const auto button = static_cast<GamepadButton>(i);
    REQUIRE(gamepadButtonFromName(toString(button)) == button);
  }
}
