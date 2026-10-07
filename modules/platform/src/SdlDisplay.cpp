#include "SdlDisplay.h"

namespace sonnet::platform {

std::optional<Display> displayFromSdl(SDL_DisplayID id) {
  SDL_Rect bounds{};
  if (id == 0 || !SDL_GetDisplayBounds(id, &bounds)) {
    return std::nullopt;
  }
  SDL_Rect usable = bounds; // a driver without usable bounds offers the whole display
  if (!SDL_GetDisplayUsableBounds(id, &usable) || usable.w <= 0 || usable.h <= 0) {
    usable = bounds;
  }
  const char *name = SDL_GetDisplayName(id);
  return Display{.id = id,
                 .name = name != nullptr ? name : "",
                 .position = {bounds.x, bounds.y},
                 .size = {static_cast<unsigned>(bounds.w), static_cast<unsigned>(bounds.h)},
                 .usablePosition = {usable.x, usable.y},
                 .usableSize = {static_cast<unsigned>(usable.w), static_cast<unsigned>(usable.h)},
                 .primary = id == SDL_GetPrimaryDisplay()};
}

} // namespace sonnet::platform
