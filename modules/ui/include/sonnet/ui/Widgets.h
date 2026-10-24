#pragma once

#include <sonnet/ui/Theme.h>

#include <imgui.h>

#include <initializer_list>
#include <string_view>

// The editor's widgets: the patterns the panels repeat, drawn from the theme (docs/editor.md, "The ui module";
// ADR-0036). They use the public Dear ImGui API and read `currentTheme()`; the widths and colours they take from it
// follow the display scale. A widget never pushes an ID of its own: each item's ID is its label in the caller's ID
// stack, which is how the editor's tests find items. Panels call these for appearance and keep calling `ImGui::` for
// behaviour the widgets do not cover.
namespace sonnet::ui {

// A base size from the theme, in pixels at the display scale (`currentScale()`). Tokens are unscaled (applyTheme
// multiplies the ones that live in ImGuiStyle), so a width that no ImGuiStyle field holds goes through here.
[[nodiscard]] float scaled(float base);

// -- Dialog buttons ----------------------------------------------------------------------------------------------

enum class DialogButtonFlags : unsigned {
  None = 0,
  // Pressed by Enter as well as by a click.
  Default = 1U << 0,
  // Pressed by Escape as well as by a click.
  Cancel = 1U << 1,
  // Drawn in the theme's `stop` colour: the button throws something away.
  Destructive = 1U << 2,
  // The theme's wide width for a label the normal one would clip.
  Wide = 1U << 3,
};

constexpr DialogButtonFlags operator|(DialogButtonFlags a, DialogButtonFlags b) {
  return static_cast<DialogButtonFlags>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
}

struct DialogButton {
  const char *label;
  DialogButtonFlags flags{DialogButtonFlags::None};
  // A disabled button is drawn so and neither a click nor its key presses it.
  bool enabled{true};
};

// A modal's buttons on one line, each `dialogButtonWidth` wide (`dialogButtonWideWidth` when Wide). Returns the index
// of the button pressed this frame, by a click or by the key of its role, or -1.
[[nodiscard]] int dialogButtons(std::initializer_list<DialogButton> buttons);

// -- Toggle button -----------------------------------------------------------------------------------------------

// A button that stays drawn pressed while `selected`, for a choice among a few (a List and a Grid view). Returns
// whether it was clicked; the caller decides what a click on the selected one means.
[[nodiscard]] bool toggleButton(const char *label, bool selected);

// -- State buttons -----------------------------------------------------------------------------------------------

enum class ButtonState {
  Normal,
  Stop,   // the Stop button while playing: the theme's `stop`
  Paused, // the Resume button while paused: the theme's `paused`
};

// A small button for a bar, in the colour of the state it shows. Returns whether it was clicked.
[[nodiscard]] bool stateButton(const char *label, ButtonState state);

// -- Status text -------------------------------------------------------------------------------------------------

enum class Status {
  Error,   // the theme's `danger`
  Warning, // the theme's `warning`
};

// A message in the colour of its status, wrapped at the right edge of the space it is drawn in unless `wrap` is off.
void statusText(Status status, std::string_view message, bool wrap = true);

// -- Property table ----------------------------------------------------------------------------------------------

// A table of one property: its name in a column of `propertyLabelWidth`, its value stretching over the rest and, with
// `revertColumn`, a column of `propertyRevertWidth` for a revert button. The caller fills the first row, starting with
// `ImGui::TableNextRow()` and `ImGui::TableNextColumn()`, and calls `endPropertyTable` when this returned true. `id`
// is the table's ID in the caller's stack.
[[nodiscard]] bool beginPropertyTable(const char *id, bool revertColumn = false);
void endPropertyTable();

// -- Overlay -----------------------------------------------------------------------------------------------------

// The distance from the corner of a view to an overlay drawn in it.
[[nodiscard]] ImVec2 overlayInset();

// A child that fits its contents over the theme's `overlay` backdrop and takes no input, for text drawn over the
// viewport. Like BeginChild, `endOverlay` is called whatever this returned.
bool beginOverlay(const char *id);
void endOverlay();

// A line of text on the theme's `label` backdrop in the bottom-left corner of the area of `areaSize` at `areaMin` (in
// screen space), clipped to the area. Sizes follow the font.
void overlayLabel(ImDrawList &drawList, ImVec2 areaMin, ImVec2 areaSize, const char *text);

} // namespace sonnet::ui
