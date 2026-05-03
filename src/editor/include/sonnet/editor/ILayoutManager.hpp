#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace sonnet::editor {

class ILayoutManager {
public:
    virtual ~ILayoutManager() = default;

    // Returns all available saved layout names (alphabetical order).
    [[nodiscard]] virtual std::vector<std::string> listLayouts() const = 0;

    // Saves current ImGui ini state under `name`. Overwrites if it already exists.
    // Returns false if the name is invalid or write fails.
    virtual bool saveLayout(std::string_view name) = 0;

    // Loads the layout named `name` into ImGui state.
    // Returns false if not found or file is unreadable/corrupt (falls back to default).
    virtual bool loadLayout(std::string_view name) = 0;

    // Returns the name of the most recently active layout, or empty string if none.
    [[nodiscard]] virtual std::string activeLayoutName() const = 0;

    // Restores the last active layout on startup; falls back to default if unavailable.
    virtual void restoreLastLayout() = 0;

    // Resets ImGui layout state to the built-in default without deleting saved layouts.
    virtual void resetToDefault() = 0;
};

} // namespace sonnet::editor
