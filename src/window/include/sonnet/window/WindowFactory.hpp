#pragma once

#include <memory>
#include <string_view>

#include <sonnet/window/IWindow.hpp>

namespace sonnet::window {

std::unique_ptr<IWindow> createDefaultWindow();
std::unique_ptr<IWindow> createWindow(int width, int height, std::string_view title);

} // namespace sonnet::window
