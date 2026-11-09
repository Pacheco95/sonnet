#include "SdlGamepads.h"

#include <sonnet/platform/Event.h>
#include <sonnet/platform/InputState.h>
#include <sonnet/platform/Platform.h>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_joystick.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <variant>
#include <vector>

using namespace sonnet::platform;

namespace {

// Two vendor ids no real device uses, so the tests see their own pads and nothing plugged into the
// machine that runs them.
constexpr Uint16 Vendor = 0x5e1d;
constexpr Uint16 FirstProduct = 0x0001;
constexpr Uint16 SecondProduct = 0x0002;

// SDL with its gamepad subsystem on, which a headless Platform leaves off, and a virtual pad per
// product id, detached at the end.
struct VirtualPads {
  Platform platform{{.headless = true}};
  std::vector<SDL_JoystickID> attached;
  std::vector<SDL_Joystick *> joysticks;

  VirtualPads() {
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT, "0x5e1d/0x0001,0x5e1d/0x0002");
    REQUIRE(SDL_InitSubSystem(SDL_INIT_GAMEPAD));
  }
  ~VirtualPads() {
    for (SDL_Joystick *joystick : joysticks) {
      SDL_CloseJoystick(joystick);
    }
    for (SDL_JoystickID id : attached) {
      SDL_DetachVirtualJoystick(id);
    }
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    SDL_ResetHint(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT);
  }
  VirtualPads(const VirtualPads &) = delete;
  VirtualPads &operator=(const VirtualPads &) = delete;

  SDL_Joystick *attach(Uint16 product, const char *name) {
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.vendor_id = Vendor;
    desc.product_id = product;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.name = name;
    const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    REQUIRE(id != 0);
    attached.push_back(id);
    SDL_Joystick *joystick = SDL_OpenJoystick(id);
    REQUIRE(joystick != nullptr);
    joysticks.push_back(joystick);
    return joystick;
  }

  void detach(SDL_Joystick *joystick) {
    const SDL_JoystickID id = SDL_GetJoystickID(joystick);
    std::erase(joysticks, joystick);
    SDL_CloseJoystick(joystick);
    std::erase(attached, id);
    REQUIRE(SDL_DetachVirtualJoystick(id));
  }
};

// Everything SDL has queued, through the gamepads into the input state; the engine events it gave.
std::vector<Event> pump(SdlGamepads &gamepads, InputState &input) {
  std::vector<Event> out;
  SDL_UpdateJoysticks();
  SDL_PumpEvents();
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    for (const Event &translated : gamepads.handle(event)) {
      input.handle(translated);
      out.push_back(translated);
    }
  }
  return out;
}

template <typename T> int count(const std::vector<Event> &events) {
  int n = 0;
  for (const Event &event : events) {
    n += std::holds_alternative<T>(event) ? 1 : 0;
  }
  return n;
}

} // namespace

TEST_CASE("SDL's gamepad buttons and axes map to the engine's", "[platform][input][gamepad]") {
  REQUIRE(gamepadButtonFromSdl(SDL_GAMEPAD_BUTTON_SOUTH) == GamepadButton::South);
  REQUIRE(gamepadButtonFromSdl(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) == GamepadButton::DPadRight);
  REQUIRE(gamepadButtonFromSdl(SDL_GAMEPAD_BUTTON_MISC1) == GamepadButton::Count);
  REQUIRE(gamepadButtonFromSdl(SDL_GAMEPAD_BUTTON_INVALID) == GamepadButton::Count);
  REQUIRE(gamepadAxisFromSdl(SDL_GAMEPAD_AXIS_RIGHTY) == GamepadAxis::RightY);
  REQUIRE(gamepadAxisFromSdl(SDL_GAMEPAD_AXIS_INVALID) == GamepadAxis::Count);
  // Full pushes reach exactly 1, SDL's longer negative end included; the sticks' y is flipped to up.
  CHECK(gamepadAxisValue(GamepadAxis::LeftX, SDL_JOYSTICK_AXIS_MAX) == 1.0f);
  CHECK(gamepadAxisValue(GamepadAxis::LeftX, SDL_JOYSTICK_AXIS_MIN) == -1.0f);
  CHECK(gamepadAxisValue(GamepadAxis::LeftY, SDL_JOYSTICK_AXIS_MIN) == 1.0f);
  CHECK(gamepadAxisValue(GamepadAxis::RightY, 16384) == Catch::Approx(-0.5f).margin(1e-4f));
  CHECK(gamepadAxisValue(GamepadAxis::LeftTrigger, 0) == 0.0f);
  CHECK(gamepadAxisValue(GamepadAxis::RightTrigger, SDL_JOYSTICK_AXIS_MAX) == 1.0f);
  CHECK(gamepadAxisValue(GamepadAxis::RightTrigger, -100) == 0.0f);
}

TEST_CASE("the first gamepad is the active one, and another takes over when it goes", "[platform][input][gamepad]") {
  VirtualPads pads;
  SdlGamepads gamepads;
  InputState input;
  REQUIRE(gamepads.handle(SDL_Event{.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN}).empty()); // none open yet

  SDL_Joystick *first = pads.attach(FirstProduct, "First pad");
  std::vector<Event> events = pump(gamepads, input);
  REQUIRE(count<GamepadConnected>(events) == 1);
  CHECK(std::get<GamepadConnected>(events.front()).name == "First pad");
  CHECK(count<GamepadAxisMoved>(events) == static_cast<int>(GamepadAxis::Count)); // its axes as they are
  REQUIRE(input.gamepadConnected());
  REQUIRE(gamepads.active() == SDL_GetJoystickID(first));

  // Its controls reach the state, the sticks' y up.
  input.beginFrame();
  REQUIRE(SDL_SetJoystickVirtualButton(first, SDL_GAMEPAD_BUTTON_SOUTH, true));
  REQUIRE(SDL_SetJoystickVirtualAxis(first, SDL_GAMEPAD_AXIS_LEFTY, SDL_JOYSTICK_AXIS_MIN));
  REQUIRE(SDL_SetJoystickVirtualAxis(first, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_JOYSTICK_AXIS_MAX));
  pump(gamepads, input);
  CHECK(input.gamepadPressed(GamepadButton::South));
  CHECK(input.leftStick().y == Catch::Approx(1.0f));
  CHECK(input.rightTrigger() == 1.0f);

  // A second pad waits: its controls are not heard while the first is active.
  SDL_Joystick *second = pads.attach(SecondProduct, "Second pad");
  input.beginFrame();
  REQUIRE(SDL_SetJoystickVirtualButton(second, SDL_GAMEPAD_BUTTON_NORTH, true));
  events = pump(gamepads, input);
  CHECK(count<GamepadConnected>(events) == 0);
  CHECK_FALSE(input.gamepadDown(GamepadButton::North));
  REQUIRE(gamepads.active() == SDL_GetJoystickID(first));

  // The first goes: the second takes over with what it holds, and the first's are let go.
  const SDL_JoystickID secondId = SDL_GetJoystickID(second);
  input.beginFrame();
  pads.detach(first);
  events = pump(gamepads, input);
  CHECK(count<GamepadConnected>(events) == 1);
  CHECK(count<GamepadDisconnected>(events) == 0);
  CHECK(gamepads.active() == secondId);
  CHECK(input.gamepadConnected());
  CHECK(input.gamepadReleased(GamepadButton::South));
  CHECK(input.leftStick() == glm::vec2{0.0f, 0.0f});
  // A button held through the change is not seen down until it is pressed again, as a key is.
  CHECK_FALSE(input.gamepadDown(GamepadButton::North));
  input.beginFrame();
  REQUIRE(SDL_SetJoystickVirtualAxis(second, SDL_GAMEPAD_AXIS_RIGHTX, SDL_JOYSTICK_AXIS_MAX));
  pump(gamepads, input);
  CHECK(input.rightStick().x == Catch::Approx(1.0f));

  // The last one goes: no gamepad.
  pads.detach(second);
  events = pump(gamepads, input);
  CHECK(count<GamepadDisconnected>(events) == 1);
  CHECK(gamepads.active() == 0);
  CHECK_FALSE(input.gamepadConnected());
  CHECK(input.rightStick() == glm::vec2{0.0f, 0.0f});
}

TEST_CASE("a regained focus brings back the axes of a stick held through it", "[platform][input][gamepad]") {
  VirtualPads pads;
  SdlGamepads gamepads;
  InputState input;
  SDL_Joystick *pad = pads.attach(FirstProduct, "Pad");
  REQUIRE(SDL_SetJoystickVirtualAxis(pad, SDL_GAMEPAD_AXIS_LEFTX, SDL_JOYSTICK_AXIS_MAX));
  pump(gamepads, input);
  REQUIRE(input.leftStick().x == Catch::Approx(1.0f));
  input.handle(WindowFocusChanged{.focused = false});
  REQUIRE(input.leftStick() == glm::vec2{0.0f, 0.0f});
  for (const Event &event : gamepads.handle(SDL_Event{.type = SDL_EVENT_WINDOW_FOCUS_GAINED})) {
    input.handle(event);
  }
  CHECK(input.leftStick().x == Catch::Approx(1.0f));
}
