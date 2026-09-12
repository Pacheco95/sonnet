#pragma once

// Include this header in exactly one translation unit of an executable, the one that defines
// sonnet::platform::createApplication. It provides main() through SDL3's callback model so
// desktop and mobile share one lifecycle (ADR-0002).

#include <sonnet/platform/Application.h>

#define SDL_MAIN_USE_CALLBACKS 1 // NOLINT(readability-identifier-naming): SDL's switch, not ours
#include <SDL3/SDL_main.h>

namespace sonnet::platform::detail {

SDL_AppResult appInit(void **state, int argc, char **argv);
SDL_AppResult appIterate(void *state);
SDL_AppResult appEvent(void *state, SDL_Event *event);
void appQuit(void *state, SDL_AppResult result);

} // namespace sonnet::platform::detail

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
  return sonnet::platform::detail::appInit(appstate, argc, argv);
}

SDL_AppResult SDL_AppIterate(void *appstate) {
  return sonnet::platform::detail::appIterate(appstate);
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
  return sonnet::platform::detail::appEvent(appstate, event);
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
  sonnet::platform::detail::appQuit(appstate, result);
}
