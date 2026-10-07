#pragma once

#include <sonnet/platform/Window.h>

#include <SDL3/SDL_video.h>

#include <optional>

namespace sonnet::platform {

// A display as the engine describes it; empty when SDL has no bounds for the id.
[[nodiscard]] std::optional<Display> displayFromSdl(SDL_DisplayID id);

} // namespace sonnet::platform
