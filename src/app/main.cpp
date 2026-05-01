#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>
#include <SDL3/SDL.h>

#include <sonnet/logging/Logger.hpp>
#include <sonnet/window/IWindow.hpp>
#include <sonnet/window/WindowFactory.hpp>
#include <sonnet/renderer/IRenderer.hpp>
#include <sonnet/renderer/IRendererBackend.hpp>
#include <sonnet/renderer/RendererFactory.hpp>
#include <sonnet/renderer/VulkanBackendFactory.hpp>

#include <memory>

struct AppState {
    std::unique_ptr<sonnet::window::IWindow> window;
    std::unique_ptr<sonnet::renderer::IRenderer> renderer;
};

SDL_AppResult SDL_AppInit(void** appstate, int /*argc*/, char* /*argv*/[]) {
    sonnet::logging::init();

    auto window = sonnet::window::createDefaultWindow();
    if (!window->init()) {
        SONNET_LOG_ERROR("Window initialization failed");
        return SDL_APP_FAILURE;
    }

    auto renderer = sonnet::renderer::createRenderer(
        sonnet::renderer::createVulkanBackend()
    );
    if (!renderer->init(*window)) {
        SONNET_LOG_ERROR("Renderer initialization failed");
        window->shutdown();
        return SDL_APP_FAILURE;
    }

    *appstate = new AppState{std::move(window), std::move(renderer)};
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    auto* state = static_cast<AppState*>(appstate);
    state->renderer->renderFrame();
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    if (event->type == SDL_EVENT_QUIT)
        return SDL_APP_SUCCESS;

    (void)appstate;
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult /*result*/) {
    auto* state = static_cast<AppState*>(appstate);
    if (state) {
        state->renderer->shutdown();
        state->window->shutdown();
        delete state;
    }
}
