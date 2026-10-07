#include <sonnet/editor/WindowState.h>

#include <sonnet/assets/Json.h>
#include <sonnet/core/File.h>
#include <sonnet/core/Log.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <format>
#include <fstream>

namespace sonnet::editor {

namespace {

using nlohmann::json;

// No window is larger than a display can be, and one smaller than this cannot be grabbed.
constexpr unsigned MinSize = 100;
constexpr unsigned MaxSize = 32768;
// How much of the rectangle has to be on a display for the window to be reachable.
constexpr int MinVisible = 64;

template <typename T> std::optional<glm::vec<2, T>> readPair(const json &value) {
  if (!value.is_array() || value.size() != 2 || !value[0].is_number_integer() || !value[1].is_number_integer()) {
    return std::nullopt;
  }
  return glm::vec<2, T>{value[0].get<T>(), value[1].get<T>()};
}

const platform::Display &primaryOf(std::span<const platform::Display> displays) {
  const auto primary = std::ranges::find_if(displays, [](const platform::Display &display) { return display.primary; });
  return primary != displays.end() ? *primary : displays.front();
}

// The connected display the saved one is: the same name and bounds, else the same name and size (the
// monitor was moved in the arrangement), else the same name (its resolution changed).
const platform::Display *findDisplay(const WindowState &saved, std::span<const platform::Display> displays) {
  const auto named = [&](const platform::Display &display) { return display.name == saved.display; };
  if (const auto exact = std::ranges::find_if(displays,
                                              [&](const platform::Display &display) {
                                                return named(display) && display.position == saved.displayPosition &&
                                                       display.size == saved.displaySize;
                                              });
      exact != displays.end()) {
    return &*exact;
  }
  if (const auto sameSize = std::ranges::find_if(
          displays,
          [&](const platform::Display &display) { return named(display) && display.size == saved.displaySize; });
      sameSize != displays.end()) {
    return &*sameSize;
  }
  const auto sameName = std::ranges::find_if(displays, named);
  return sameName != displays.end() ? &*sameName : nullptr;
}

glm::uvec2 clampTo(glm::uvec2 size, const platform::Display &display) {
  return {std::min(size.x, display.usableSize.x), std::min(size.y, display.usableSize.y)};
}

// Whether `rect` has MinVisible pixels each way on some display's usable area.
bool isReachable(const platform::WindowRect &rect, std::span<const platform::Display> displays) {
  return std::ranges::any_of(displays, [&](const platform::Display &display) {
    const glm::ivec2 low = glm::max(rect.position, display.usablePosition);
    const glm::ivec2 high =
        glm::min(rect.position + glm::ivec2{rect.size}, display.usablePosition + glm::ivec2{display.usableSize});
    return high.x - low.x >= MinVisible && high.y - low.y >= MinVisible;
  });
}

void describe(WindowState &state, const platform::Display &display) {
  state.display = display.name;
  state.displayPosition = display.position;
  state.displaySize = display.size;
}

} // namespace

std::optional<WindowState> WindowState::load(const std::filesystem::path &file) {
  const auto bytes = core::readFile(file);
  if (!bytes) {
    return std::nullopt; // first run
  }
  const json document = assets::parseJson(*bytes);
  if (document.is_discarded() || !document.is_object()) {
    SONNET_LOG_WARN("{}: not valid JSON, the window opens at its default place", file.string());
    return std::nullopt;
  }
  WindowState state;
  const auto size = readPair<int>(document.value("size", json{}));
  const auto position = readPair<int>(document.value("position", json{}));
  const json display = document.value("display", json::object());
  if (!size || !position || !display.is_object() || !display.contains("name") || !display["name"].is_string()) {
    SONNET_LOG_WARN("{}: incomplete, the window opens at its default place", file.string());
    return std::nullopt;
  }
  const auto displayPosition = readPair<int>(display.value("position", json{}));
  const auto displaySize = readPair<int>(display.value("size", json{}));
  const auto inRange = [](int extent) {
    return extent >= static_cast<int>(MinSize) && extent <= static_cast<int>(MaxSize);
  };
  if (!displayPosition || !displaySize || !inRange(size->x) || !inRange(size->y)) {
    SONNET_LOG_WARN("{}: out of range, the window opens at its default place", file.string());
    return std::nullopt;
  }
  state.size = {static_cast<unsigned>(size->x), static_cast<unsigned>(size->y)};
  state.position = *position;
  state.maximized = document.value("maximized", false);
  state.display = display["name"].get<std::string>();
  state.displayPosition = *displayPosition;
  state.displaySize = {static_cast<unsigned>(std::max(displaySize->x, 0)),
                       static_cast<unsigned>(std::max(displaySize->y, 0))};
  return state;
}

core::Result<void> WindowState::save(const std::filesystem::path &file) const {
  const json document{
      {"size", {size.x, size.y}},
      {"position", {position.x, position.y}},
      {"maximized", maximized},
      {"display",
       {{"name", display},
        {"position", {displayPosition.x, displayPosition.y}},
        {"size", {displaySize.x, displaySize.y}}}},
  };
  std::ofstream stream{file};
  if (!stream) {
    return std::unexpected(
        core::Error{std::format("{}: cannot open for writing", file.string()), core::ErrorCategory::Io});
  }
  stream << document.dump(2) << '\n';
  return {};
}

bool WindowState::sameAs(const WindowState &other) const {
  return size == other.size && maximized == other.maximized && display == other.display &&
         displayPosition == other.displayPosition && displaySize == other.displaySize &&
         std::abs(position.x - other.position.x) <= Tolerance && std::abs(position.y - other.position.y) <= Tolerance;
}

void WindowPlacement::applyTo(platform::WindowDesc &desc) const {
  desc.size = size;
  desc.position = position;
  desc.maximized = maximized;
}

WindowPlacement placeWindow(const std::optional<WindowState> &saved, std::span<const platform::Display> displays,
                            bool canPosition) {
  WindowPlacement placement;
  if (saved) {
    placement.size = saved->size;
    placement.maximized = saved->maximized;
  }
  if (displays.empty()) {
    return placement; // nothing to clamp to or centre on
  }
  const platform::Display *target = saved ? findDisplay(*saved, displays) : nullptr;
  if (!canPosition) {
    // The compositor places the window; the size is still the user's, kept to the display it will
    // most likely open on.
    const platform::Display &display = target != nullptr ? *target : primaryOf(displays);
    placement.size = clampTo(placement.size, display);
    placement.state = saved.value_or(WindowState{});
    placement.state.size = placement.size;
    return placement;
  }
  if (saved && target != nullptr) {
    const glm::uvec2 size = clampTo(saved->size, *target);
    const platform::WindowRect rect{target->position + saved->position, size};
    if (isReachable(rect, displays)) {
      placement.size = size;
      placement.position = rect.position;
      placement.state = makeWindowState(rect, saved->maximized, *target);
      return placement;
    }
  }
  const platform::Display &primary = primaryOf(displays);
  placement.size = clampTo(placement.size, primary);
  placement.position = primary.usablePosition + (glm::ivec2{primary.usableSize} - glm::ivec2{placement.size}) / 2;
  placement.fallback = saved.has_value();
  placement.state = makeWindowState({*placement.position, placement.size}, placement.maximized, primary);
  return placement;
}

WindowState makeWindowState(const platform::WindowRect &restored, bool maximized, const platform::Display &display) {
  WindowState state;
  state.size = restored.size;
  state.position = restored.position - display.position;
  state.maximized = maximized;
  describe(state, display);
  return state;
}

std::optional<WindowState> captureWindowState(const platform::IWindow &window) {
  const std::optional<platform::Display> display = window.display();
  if (!display) {
    return std::nullopt;
  }
  const bool maximized = window.isMaximized();
  // A maximized or minimized window's own rectangle is the display's or off screen; the one to
  // restore is the tracked one.
  const platform::WindowRect rect = maximized || window.isMinimized()
                                        ? window.restoredRect()
                                        : platform::WindowRect{window.position(), window.size()};
  return makeWindowState(rect, maximized, *display);
}

} // namespace sonnet::editor
