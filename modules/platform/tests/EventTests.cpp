#include "SdlEvents.h"

#include <sonnet/platform/Event.h>

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <variant>

using namespace sonnet::platform;

namespace {

SDL_Event makeEvent(SDL_EventType type) {
  SDL_Event event;
  std::memset(&event, 0, sizeof(event));
  event.type = type;
  return event;
}

} // namespace

TEST_CASE("scancodes map to keys and unknown scancodes to Key::Unknown", "[platform][input]") {
  REQUIRE(keyFromScancode(SDL_SCANCODE_A) == Key::A);
  REQUIRE(keyFromScancode(SDL_SCANCODE_Z) == Key::Z);
  REQUIRE(keyFromScancode(SDL_SCANCODE_0) == Key::Digit0);
  REQUIRE(keyFromScancode(SDL_SCANCODE_F12) == Key::F12);
  REQUIRE(keyFromScancode(SDL_SCANCODE_ESCAPE) == Key::Escape);
  REQUIRE(keyFromScancode(SDL_SCANCODE_KP_ENTER) == Key::KeypadEnter);
  REQUIRE(keyFromScancode(SDL_SCANCODE_LGUI) == Key::LeftSuper);
  REQUIRE(keyFromScancode(SDL_SCANCODE_UNKNOWN) == Key::Unknown);
  REQUIRE(keyFromScancode(SDL_SCANCODE_MUTE) == Key::Unknown);
  REQUIRE(keyFromScancode(SDL_SCANCODE_COUNT) == Key::Unknown);
}

TEST_CASE("modifier masks fold left and right variants", "[platform][input]") {
  REQUIRE(modifiersFromSdl(SDL_KMOD_NONE) == Modifiers::None);
  const Modifiers mods = modifiersFromSdl(SDL_KMOD_LSHIFT | SDL_KMOD_RCTRL | SDL_KMOD_CAPS);
  REQUIRE(has(mods, Modifiers::Shift));
  REQUIRE(has(mods, Modifiers::Ctrl));
  REQUIRE(has(mods, Modifiers::CapsLock));
  REQUIRE(!has(mods, Modifiers::Alt));
  REQUIRE(!has(mods, Modifiers::Super));
}

TEST_CASE("key events carry key, modifiers and repeat", "[platform][input]") {
  SDL_Event down = makeEvent(SDL_EVENT_KEY_DOWN);
  down.key.scancode = SDL_SCANCODE_W;
  down.key.mod = SDL_KMOD_LSHIFT;
  down.key.repeat = true;
  const auto pressed = translateEvent(down);
  REQUIRE(pressed.has_value());
  const auto *key = std::get_if<KeyPressed>(&*pressed);
  REQUIRE(key != nullptr);
  REQUIRE(key->key == Key::W);
  REQUIRE(has(key->modifiers, Modifiers::Shift));
  REQUIRE(key->repeat);

  SDL_Event up = makeEvent(SDL_EVENT_KEY_UP);
  up.key.scancode = SDL_SCANCODE_W;
  const auto released = translateEvent(up);
  REQUIRE(released.has_value());
  REQUIRE(std::get<KeyReleased>(*released).key == Key::W);
}

TEST_CASE("mouse events carry positions, buttons and wheel direction", "[platform][input]") {
  SDL_Event motion = makeEvent(SDL_EVENT_MOUSE_MOTION);
  motion.motion.x = 10.5f;
  motion.motion.y = 20.0f;
  motion.motion.xrel = -1.0f;
  motion.motion.yrel = 2.0f;
  const auto moved = std::get<MouseMoved>(*translateEvent(motion));
  REQUIRE(moved.position == glm::vec2{10.5f, 20.0f});
  REQUIRE(moved.delta == glm::vec2{-1.0f, 2.0f});

  SDL_Event button = makeEvent(SDL_EVENT_MOUSE_BUTTON_DOWN);
  button.button.button = SDL_BUTTON_RIGHT;
  button.button.down = true;
  button.button.clicks = 2;
  button.button.x = 3.0f;
  const auto pressed = std::get<MouseButtonPressed>(*translateEvent(button));
  REQUIRE(pressed.button == MouseButton::Right);
  REQUIRE(pressed.clicks == 2);
  REQUIRE(pressed.position.x == 3.0f);

  button.type = SDL_EVENT_MOUSE_BUTTON_UP;
  button.button.down = false;
  REQUIRE(std::get<MouseButtonReleased>(*translateEvent(button)).button == MouseButton::Right);

  button.button.button = 9;
  REQUIRE(!translateEvent(button).has_value());

  SDL_Event wheel = makeEvent(SDL_EVENT_MOUSE_WHEEL);
  wheel.wheel.y = 1.0f;
  wheel.wheel.direction = SDL_MOUSEWHEEL_FLIPPED;
  REQUIRE(std::get<MouseWheel>(*translateEvent(wheel)).delta == glm::vec2{0.0f, -1.0f});
}

TEST_CASE("window and quit events translate", "[platform][input]") {
  SDL_Event resized = makeEvent(SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED);
  resized.window.data1 = 1920;
  resized.window.data2 = 1080;
  REQUIRE(std::get<WindowResized>(*translateEvent(resized)).pixelSize == glm::uvec2{1920, 1080});
  REQUIRE(std::holds_alternative<WindowCloseRequested>(*translateEvent(makeEvent(SDL_EVENT_WINDOW_CLOSE_REQUESTED))));
  REQUIRE(std::holds_alternative<QuitRequested>(*translateEvent(makeEvent(SDL_EVENT_QUIT))));
  REQUIRE(std::holds_alternative<WindowMinimized>(*translateEvent(makeEvent(SDL_EVENT_WINDOW_MINIMIZED))));
  REQUIRE(std::get<WindowFocusChanged>(*translateEvent(makeEvent(SDL_EVENT_WINDOW_FOCUS_LOST))).focused == false);
}

TEST_CASE("text input copies the UTF-8 text", "[platform][input]") {
  SDL_Event text = makeEvent(SDL_EVENT_TEXT_INPUT);
  text.text.text = "héllo";
  REQUIRE(std::get<TextInput>(*translateEvent(text)).text == "héllo");
}

TEST_CASE("events the engine does not use are dropped", "[platform][input]") {
  REQUIRE(!translateEvent(makeEvent(SDL_EVENT_CLIPBOARD_UPDATE)).has_value());
  REQUIRE(!translateEvent(makeEvent(SDL_EVENT_WINDOW_RESIZED)).has_value());
}
