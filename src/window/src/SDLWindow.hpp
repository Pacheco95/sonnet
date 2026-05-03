#pragma once

#include <sonnet/window/IWindow.hpp>

#include <SDL3/SDL.h>

namespace sonnet::window {

class SDLWindow : public IWindow {
public:
    SDLWindow(int width, int height, std::string_view title);
    ~SDLWindow() override;

    bool init() override;
    void shutdown() override;

    [[nodiscard]] bool shouldClose() const override;
    [[nodiscard]] std::pair<int, int> getSize() const override;

    [[nodiscard]] std::vector<std::string> getRequiredInstanceExtensions() const override;
    [[nodiscard]] uint64_t createSurface(uint64_t instanceHandle) const override;
    [[nodiscard]] void* getWindowHandle() const override { return m_window; }

private:
    int m_width;
    int m_height;
    std::string m_title;
    SDL_Window* m_window{nullptr};
    bool m_shouldClose{false};
    bool m_initialized{false};
};

} // namespace sonnet::window
