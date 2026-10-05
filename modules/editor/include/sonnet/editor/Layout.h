#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sonnet::editor {

// Which panels are shown and which scene overlays are drawn: the view state a layout preset carries
// beside the dock tree (docs/editor.md, "Layout presets").
struct ViewState {
  bool viewport{true};
  bool game{false};
  bool hierarchy{true};
  bool inspector{true};
  bool log{true};
  bool assets{true};
  bool statistics{true};
  bool overlay{true};
  bool colliders{false};
  bool lightGizmos{false};

  [[nodiscard]] bool operator==(const ViewState &) const noexcept = default;
};

// The two layouts built in code. Their names are reserved: a saved preset cannot take them.
enum class BuiltinLayout : std::uint8_t {
  Default,
  Play,
};

[[nodiscard]] std::string_view layoutName(BuiltinLayout layout) noexcept;
// The built-in a name stands for, compared without regard to case (the preset files may live on a
// case-insensitive file system).
[[nodiscard]] std::optional<BuiltinLayout> builtinLayout(std::string_view name) noexcept;
[[nodiscard]] ViewState builtinViewState(BuiltinLayout layout) noexcept;

// A preset name as a file name: letters, digits, space, '-' and '_' are kept, anything else
// becomes '_', and the edges are trimmed. Empty when nothing usable is left.
[[nodiscard]] std::string sanitizeLayoutName(std::string_view name);

// The saved presets in `directory` (the stems of its .ini files), sorted.
[[nodiscard]] std::vector<std::string> listLayouts(const std::filesystem::path &directory);

// Whether `ini` is a layout this editor wrote: it has the view section and a dock tree with a root.
[[nodiscard]] bool isLayoutIni(std::string_view ini) noexcept;

// `ini` with every dock Size and SizeRef scaled by the given work size over the size the preset
// recorded for the dock root. ImGui keeps the saved absolute split sizes and only clamps the root,
// so a preset saved on another display size would otherwise load with distorted splits. Returned
// unchanged when the preset recorded no size.
[[nodiscard]] std::string scaleLayoutIni(std::string_view ini, float width, float height);

// Teaches the current ImGui context's ini to carry a ViewState, so layout.ini and the presets
// restore it with the dock tree, and to record the dock root's size. Lives as long as the editor.
class LayoutSettings {
public:
  explicit LayoutSettings(ViewState &view);
  ~LayoutSettings();
  LayoutSettings(const LayoutSettings &) = delete;
  LayoutSettings &operator=(const LayoutSettings &) = delete;

  // The dock root, whose size is recorded; 0 before the first frame.
  void setDockspace(unsigned id) noexcept {
    m_dockspace = id;
  }
  [[nodiscard]] unsigned dockspace() const noexcept {
    return m_dockspace;
  }

private:
  ViewState &m_view;
  unsigned m_dockspace{0};
};

} // namespace sonnet::editor
