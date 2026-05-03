#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace sonnet::window {

class IWindow {
public:
    virtual ~IWindow() = default;

    virtual bool init() = 0;
    virtual void shutdown() = 0;

    [[nodiscard]] virtual bool shouldClose() const = 0;
    [[nodiscard]] virtual std::pair<int, int> getSize() const = 0;

    [[nodiscard]] virtual std::vector<std::string> getRequiredInstanceExtensions() const = 0;
    [[nodiscard]] virtual uint64_t createSurface(uint64_t instanceHandle) const = 0;

    // Returns the native window handle (e.g. SDL_Window*) for editor UI backend init.
    [[nodiscard]] virtual void* getWindowHandle() const { return nullptr; }
};

} // namespace sonnet::window
