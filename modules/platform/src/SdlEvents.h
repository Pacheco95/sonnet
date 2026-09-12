#pragma once

#include <sonnet/platform/Event.h>
#include <sonnet/platform/Input.h>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_scancode.h>

#include <optional>

namespace sonnet::platform {

[[nodiscard]] Key keyFromScancode(SDL_Scancode scancode) noexcept;
[[nodiscard]] Modifiers modifiersFromSdl(SDL_Keymod mods) noexcept;
// Events the engine has no use for map to nullopt.
[[nodiscard]] std::optional<Event> translateEvent(const SDL_Event &event);

} // namespace sonnet::platform
