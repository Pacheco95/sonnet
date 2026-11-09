#pragma once

#include <sonnet/platform/Event.h>
#include <sonnet/platform/Input.h>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>

#include <array>
#include <cstddef>
#include <optional>
#include <span>

namespace sonnet::platform {

// SDL's gamepad controls in the engine's terms; buttons the engine has no name for map to Count.
[[nodiscard]] GamepadButton gamepadButtonFromSdl(SDL_GamepadButton button) noexcept;
[[nodiscard]] GamepadAxis gamepadAxisFromSdl(SDL_GamepadAxis axis) noexcept;
// SDL's raw axis value in the axis's engine range: a stick from -1 to 1 with +y up, a trigger from
// 0 to 1 (docs/platform.md, "Gamepads").
[[nodiscard]] float gamepadAxisValue(GamepadAxis axis, Sint16 value) noexcept;

// The one active gamepad (docs/platform.md, "Gamepads"): which gamepad it is, opening and closing
// it, and its SDL events as engine events. The entry point owns one between the Platform, which
// initialises SDL's gamepad subsystem, and the application.
class SdlGamepads {
public:
  // A gamepad's arrival: GamepadConnected, then the value of each of its six axes.
  static constexpr std::size_t MaxEvents = 1 + static_cast<std::size_t>(GamepadAxis::Count);

  SdlGamepads() = default;
  ~SdlGamepads();
  SdlGamepads(const SdlGamepads &) = delete;
  SdlGamepads &operator=(const SdlGamepads &) = delete;

  // The engine events an SDL event adds, in order, valid until the next call; empty for most. The
  // window's focus returning adds the active gamepad's axes as SDL holds them, after the focus
  // event that translateEvent gives.
  std::span<const Event> handle(const SDL_Event &event);

  // The active gamepad's SDL instance id, 0 when there is none.
  [[nodiscard]] SDL_JoystickID active() const noexcept {
    return m_activeId;
  }

private:
  // Opens the first of SDL's gamepads but `except` that opens; false when none does.
  bool openFirst(SDL_JoystickID except);
  bool open(SDL_JoystickID id);
  void close() noexcept;
  void emitAxes();
  void emit(Event event);

  SDL_Gamepad *m_active{nullptr};
  SDL_JoystickID m_activeId{0};
  std::array<Event, MaxEvents> m_events{};
  std::size_t m_count{0};
};

} // namespace sonnet::platform
