#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <sonnet/editor/EditorFactory.hpp>
#include <sonnet/editor/IEditor.hpp>
#include <sonnet/logging/Logger.hpp>
#include <sonnet/renderer/IRendererBackend.hpp>
#include <sonnet/renderer/VulkanBackendFactory.hpp>
#include <sonnet/window/IWindow.hpp>
#include <sonnet/window/WindowFactory.hpp>

#include <filesystem>
#include <memory>

struct AppState {
    std::unique_ptr<sonnet::window::IWindow> window;
    std::unique_ptr<sonnet::renderer::IRendererBackend> backend;
    std::unique_ptr<sonnet::editor::IEditor> editor;
};

SDL_AppResult SDL_AppInit(void** appstate, int /*argc*/, char* argv[]) {
    sonnet::logging::init();

    auto window = sonnet::window::createDefaultWindow();
    if (!window->init()) {
        SONNET_LOG_ERROR("Window initialization failed");
        return SDL_APP_FAILURE;
    }

    auto backend = sonnet::renderer::createVulkanBackend();
    if (!backend->init(*window)) {
        SONNET_LOG_ERROR("Renderer backend initialization failed");
        window->shutdown();
        return SDL_APP_FAILURE;
    }

    std::filesystem::path exeDir;
    try {
        exeDir = std::filesystem::canonical("/proc/self/exe").parent_path();
    } catch (...) {
        exeDir = std::filesystem::path(argv[0]).parent_path(); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    }

    auto editor = sonnet::editor::createEditor(exeDir / "layouts");
    if (!editor->init(*window, *backend)) {
        SONNET_LOG_ERROR("Editor initialization failed");
        backend->shutdown();
        window->shutdown();
        return SDL_APP_FAILURE;
    }

    *appstate = new AppState{std::move(window), std::move(backend), std::move(editor)};
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    auto* state = static_cast<AppState*>(appstate);
    state->backend->renderSceneOffscreen();
    state->editor->render();
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;
    }

    auto* state = static_cast<AppState*>(appstate);
    state->editor->processEvent(event);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult /*result*/) {
    auto* state = static_cast<AppState*>(appstate);
    if (state != nullptr) {
        state->editor->shutdown();
        state->backend->shutdown();
        state->window->shutdown();
        delete state;
    }
}
