#include "SdlGamepads.h"

#include <sonnet/core/Log.h>

#include <SDL3/SDL_stdinc.h>

#include <algorithm>

namespace sonnet::platform {

GamepadButton gamepadButtonFromSdl(SDL_GamepadButton button) noexcept {
  switch (button) {
  case SDL_GAMEPAD_BUTTON_SOUTH:
    return GamepadButton::South;
  case SDL_GAMEPAD_BUTTON_EAST:
    return GamepadButton::East;
  case SDL_GAMEPAD_BUTTON_WEST:
    return GamepadButton::West;
  case SDL_GAMEPAD_BUTTON_NORTH:
    return GamepadButton::North;
  case SDL_GAMEPAD_BUTTON_BACK:
    return GamepadButton::Back;
  case SDL_GAMEPAD_BUTTON_GUIDE:
    return GamepadButton::Guide;
  case SDL_GAMEPAD_BUTTON_START:
    return GamepadButton::Start;
  case SDL_GAMEPAD_BUTTON_LEFT_STICK:
    return GamepadButton::LeftStick;
  case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
    return GamepadButton::RightStick;
  case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
    return GamepadButton::LeftShoulder;
  case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
    return GamepadButton::RightShoulder;
  case SDL_GAMEPAD_BUTTON_DPAD_UP:
    return GamepadButton::DPadUp;
  case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
    return GamepadButton::DPadDown;
  case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
    return GamepadButton::DPadLeft;
  case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
    return GamepadButton::DPadRight;
  default:
    // The paddles, the misc buttons and the touchpad's click have no engine name yet.
    return GamepadButton::Count;
  }
}

GamepadAxis gamepadAxisFromSdl(SDL_GamepadAxis axis) noexcept {
  switch (axis) {
  case SDL_GAMEPAD_AXIS_LEFTX:
    return GamepadAxis::LeftX;
  case SDL_GAMEPAD_AXIS_LEFTY:
    return GamepadAxis::LeftY;
  case SDL_GAMEPAD_AXIS_RIGHTX:
    return GamepadAxis::RightX;
  case SDL_GAMEPAD_AXIS_RIGHTY:
    return GamepadAxis::RightY;
  case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
    return GamepadAxis::LeftTrigger;
  case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:
    return GamepadAxis::RightTrigger;
  default:
    return GamepadAxis::Count;
  }
}

float gamepadAxisValue(GamepadAxis axis, Sint16 value) noexcept {
  // SDL's range is one longer below zero; dividing by its top keeps a full push at exactly 1.
  const float unit = std::clamp(static_cast<float>(value) / static_cast<float>(SDL_JOYSTICK_AXIS_MAX), -1.0f, 1.0f);
  switch (axis) {
  case GamepadAxis::LeftY:
  case GamepadAxis::RightY:
    // SDL's sticks are positive down, towards the player; the engine's are positive up.
    return -unit;
  case GamepadAxis::LeftTrigger:
  case GamepadAxis::RightTrigger:
    return std::max(unit, 0.0f);
  default:
    return unit;
  }
}

SdlGamepads::~SdlGamepads() {
  close();
}

std::span<const Event> SdlGamepads::handle(const SDL_Event &event) {
  m_count = 0;
  switch (event.type) {
  case SDL_EVENT_GAMEPAD_ADDED:
    // SDL reports every gamepad present at start as added too. The first one to open stays the
    // active one until it goes; later ones wait.
    if (m_active == nullptr) {
      static_cast<void>(open(event.gdevice.which));
    }
    break;
  case SDL_EVENT_GAMEPAD_REMOVED:
    if (event.gdevice.which == m_activeId) {
      close();
      if (!openFirst(event.gdevice.which)) {
        SONNET_LOG_INFO("gamepad disconnected");
        emit(GamepadDisconnected{});
      }
    }
    break;
  case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
  case SDL_EVENT_GAMEPAD_BUTTON_UP:
    if (event.gbutton.which == m_activeId && m_active != nullptr) {
      const GamepadButton button = gamepadButtonFromSdl(static_cast<SDL_GamepadButton>(event.gbutton.button));
      if (button != GamepadButton::Count) {
        if (event.gbutton.down) {
          emit(GamepadButtonPressed{button});
        } else {
          emit(GamepadButtonReleased{button});
        }
      }
    }
    break;
  case SDL_EVENT_GAMEPAD_AXIS_MOTION:
    if (event.gaxis.which == m_activeId && m_active != nullptr) {
      const GamepadAxis axis = gamepadAxisFromSdl(static_cast<SDL_GamepadAxis>(event.gaxis.axis));
      if (axis != GamepadAxis::Count) {
        emit(GamepadAxisMoved{axis, gamepadAxisValue(axis, event.gaxis.value)});
      }
    }
    break;
  case SDL_EVENT_WINDOW_FOCUS_GAINED:
    // A lost focus centres the axes in the input state, and a stick held through the return sends
    // no motion: its value as SDL holds it now stands in.
    if (m_active != nullptr) {
      emitAxes();
    }
    break;
  default:
    break;
  }
  return std::span{m_events}.first(m_count);
}

bool SdlGamepads::openFirst(SDL_JoystickID except) {
  int count = 0;
  SDL_JoystickID *ids = SDL_GetGamepads(&count);
  if (ids == nullptr) {
    return false;
  }
  bool opened = false;
  for (int i = 0; i < count && !opened; ++i) {
    opened = ids[i] != except && open(ids[i]);
  }
  SDL_free(ids);
  return opened;
}

bool SdlGamepads::open(SDL_JoystickID id) {
  SDL_Gamepad *gamepad = SDL_OpenGamepad(id);
  if (gamepad == nullptr) {
    SONNET_LOG_WARN("could not open gamepad {}: {}", id, SDL_GetError());
    return false;
  }
  m_active = gamepad;
  m_activeId = id;
  const char *name = SDL_GetGamepadName(gamepad);
  SONNET_LOG_INFO("gamepad \"{}\" connected", name != nullptr ? name : "");
  emit(GamepadConnected{name != nullptr ? name : ""});
  emitAxes();
  return true;
}

void SdlGamepads::close() noexcept {
  if (m_active != nullptr) {
    SDL_CloseGamepad(m_active);
  }
  m_active = nullptr;
  m_activeId = 0;
}

void SdlGamepads::emitAxes() {
  for (std::size_t i = 0; i < static_cast<std::size_t>(GamepadAxis::Count); ++i) {
    const auto axis = static_cast<GamepadAxis>(i);
    constexpr SDL_GamepadAxis sdlAxes[] = {SDL_GAMEPAD_AXIS_LEFTX,        SDL_GAMEPAD_AXIS_LEFTY,
                                           SDL_GAMEPAD_AXIS_RIGHTX,       SDL_GAMEPAD_AXIS_RIGHTY,
                                           SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER};
    emit(GamepadAxisMoved{axis, gamepadAxisValue(axis, SDL_GetGamepadAxis(m_active, sdlAxes[i]))});
  }
}

void SdlGamepads::emit(Event event) {
  if (m_count < m_events.size()) {
    m_events[m_count++] = std::move(event);
  }
}

} // namespace sonnet::platform
