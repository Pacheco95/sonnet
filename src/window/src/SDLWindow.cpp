#include "SDLWindow.hpp"

#include <sonnet/logging/Logger.hpp>

#include <SDL3/SDL_vulkan.h>

#include <vulkan/vulkan.h>

namespace sonnet::window {

SDLWindow::SDLWindow(int width, int height, // NOLINT(bugprone-easily-swappable-parameters)
                     std::string_view title)
    : m_width(width), m_height(height), m_title(title) {}

SDLWindow::~SDLWindow() { shutdown(); } // NOLINT(clang-analyzer-optin.cplusplus.VirtualCall)

bool SDLWindow::init() {
    if (m_initialized) {
        return true;
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SONNET_LOG_ERROR("Failed to initialize SDL: {}", SDL_GetError());
        return false;
    }

    m_window = SDL_CreateWindow(m_title.c_str(), m_width, m_height,
                                SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

    if (m_window == nullptr) {
        SONNET_LOG_ERROR("Failed to create window: {}", SDL_GetError());
        SDL_Quit();
        return false;
    }

    m_initialized = true;
    SONNET_LOG_INFO("Window created: {}x{} \"{}\"", m_width, m_height, m_title);
    return true;
}

void SDLWindow::shutdown() {
    if (!m_initialized) {
        return;
    }
    m_initialized = false;

    if (m_window != nullptr) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
    SDL_Quit();
}

bool SDLWindow::shouldClose() const { return m_shouldClose; }

std::pair<int, int> SDLWindow::getSize() const {
    int width = m_width;
    int height = m_height;
    if (m_window != nullptr) {
        SDL_GetWindowSize(m_window, &width, &height);
    }
    return {width, height};
}

std::vector<std::string> SDLWindow::getRequiredInstanceExtensions() const {
    Uint32 count{};
    const char* const* exts = SDL_Vulkan_GetInstanceExtensions(&count);
    if (exts == nullptr) {
        SONNET_LOG_ERROR("Failed to get Vulkan instance extensions: {}", SDL_GetError());
        return {};
    }
    return {exts, exts + count};
}

uint64_t SDLWindow::createSurface(uint64_t instanceHandle) const {
    auto* instance =
        reinterpret_cast<VkInstance>(instanceHandle); // NOLINT(performance-no-int-to-ptr)
    VkSurfaceKHR surface{VK_NULL_HANDLE};
    if (!SDL_Vulkan_CreateSurface(m_window, instance, nullptr, &surface)) {
        SONNET_LOG_ERROR("Failed to create Vulkan surface: {}", SDL_GetError());
        return 0;
    }
    return reinterpret_cast<uint64_t>(surface); // NOLINT(performance-no-int-to-ptr)
}

} // namespace sonnet::window
