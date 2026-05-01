#include <sonnet/window/WindowFactory.hpp>

#include "SDLWindow.hpp"

namespace sonnet::window {

static constexpr int kDefaultWidth = 800;
static constexpr int kDefaultHeight = 600;

std::unique_ptr<IWindow> createDefaultWindow() {
    return std::make_unique<SDLWindow>(kDefaultWidth, kDefaultHeight, "Sonnet Engine");
}

std::unique_ptr<IWindow> createWindow(int width, int height, std::string_view title) {
    return std::make_unique<SDLWindow>(width, height, title);
}

} // namespace sonnet::window
