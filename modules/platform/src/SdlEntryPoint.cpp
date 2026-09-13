#include <sonnet/platform/Application.h>

#include "SdlEvents.h"

#include <sonnet/core/Log.h>
#include <sonnet/core/Profile.h>
#include <sonnet/core/Version.h>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>

#include <algorithm>
#include <exception>
#include <memory>
#include <string_view>
#include <vector>

namespace sonnet::platform::detail {

namespace {

// Platform is declared first so it outlives the application.
struct AppState {
  Platform platform;
  std::unique_ptr<IApplication> app;
};

SDL_AppResult toSdl(AppResult result) noexcept {
  switch (result) {
  case AppResult::Continue:
    return SDL_APP_CONTINUE;
  case AppResult::Success:
    return SDL_APP_SUCCESS;
  case AppResult::Failure:
    return SDL_APP_FAILURE;
  }
  return SDL_APP_FAILURE;
}

} // namespace

SDL_AppResult appInit(void **state, int argc, char **argv) {
  core::Log::init();
  SONNET_LOG_INFO("Sonnet {}", core::engineVersion().toString());
  try {
    auto appState = std::make_unique<AppState>();
    // The arguments proper: the program name is of no use to an application.
    std::vector<std::string_view> args(argv + std::min(argc, 1), argv + argc);
    appState->app = createApplication(appState->platform, args);
    // SDL holds the state between callbacks; appQuit takes ownership back.
    *state = appState.release();
    return SDL_APP_CONTINUE;
  } catch (const std::exception &e) {
    SONNET_LOG_CRITICAL("startup failed: {}", e.what());
    core::Log::flush();
    return SDL_APP_FAILURE;
  }
}

SDL_AppResult appIterate(void *state) {
  auto *appState = static_cast<AppState *>(state);
  const AppResult result = appState->app->iterate();
  SONNET_FRAME_MARK();
  return toSdl(result);
}

SDL_AppResult appEvent(void *state, SDL_Event *event) {
  auto *appState = static_cast<AppState *>(state);
  appState->app->nativeEvent(*event);
  const std::optional<Event> translated = translateEvent(*event);
  if (!translated) {
    return SDL_APP_CONTINUE;
  }
  return toSdl(appState->app->event(*translated));
}

void appQuit(void *state, SDL_AppResult result) {
  std::unique_ptr<AppState> appState{static_cast<AppState *>(state)};
  if (appState) {
    appState->app.reset();
  }
  appState.reset();
  SONNET_LOG_INFO("exit {}", result == SDL_APP_SUCCESS ? "ok" : "with failure");
  core::Log::flush();
}

} // namespace sonnet::platform::detail
