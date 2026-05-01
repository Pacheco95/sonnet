#include <sonnet/window/WindowFactory.hpp>

#include "SDLWindow.hpp"

namespace sonnet::window {

std::unique_ptr<IWindow> createDefaultWindow() {
    return std::make_unique<SDLWindow>(800, 600, "Sonnet Engine");
}

std::unique_ptr<IWindow> createWindow(int width, int height, std::string_view title) {
    return std::make_unique<SDLWindow>(width, height, title);
}

} // namespace sonnet::window
