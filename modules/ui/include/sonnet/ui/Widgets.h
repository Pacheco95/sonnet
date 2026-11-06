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

// `count` text heights at the current font size: the width of what holds text (a filter field, a combo, a column of
// names), which follows the text whatever drives it. Geometry that does not hold text (a line's thickness, a pick
// distance) goes through `scaled`. Call it inside a frame.
[[nodiscard]] float em(float count);

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

// A button for the menu bar, in the colour of the state it shows. It has a button's frame padding, so it is as tall as
// the bar. Returns whether it was clicked.
[[nodiscard]] bool stateButton(const char *label, ButtonState state);

// -- Status text -------------------------------------------------------------------------------------------------

enum class Status {
  Error,   // the theme's `danger`
  Warning, // the theme's `warning`
};

// A message in the colour of its status, wrapped at the right edge of the space it is drawn in unless `wrap` is off.
void statusText(Status status, std::string_view message, bool wrap = true);

// -- Text roles --------------------------------------------------------------------------------------------------

// A hint, a caption or a read-only value, in the theme's `textDisabled`. Formatted as ImGui::Text.
void mutedText(const char *format, ...) IM_FMTARGS(1);

// Text in the theme's `accent`, which marks state: a value that differs from the prefab.
void markedText(std::string_view text);

enum class TreeRow {
  Normal,
  // In the theme's `textDisabled`: a row shown only for what is below it, such as an ancestor of a filter's match.
  Dimmed,
};

// ImGui::TreeNodeEx with `id` as the item's ID and `label` as its text, drawn in the colour of `row`. Returns whether
// the node is open; the caller pops it as for TreeNodeEx.
[[nodiscard]] bool treeNode(const char *id, ImGuiTreeNodeFlags flags, const char *label, TreeRow row = TreeRow::Normal);

// -- Section header ----------------------------------------------------------------------------------------------

// A collapsible section's header (a component, a clip): a flat band in the theme's `sectionHeader` colours, with no
// frame outline. ImGuiCol_Header is also the selected rows' colour, which stays `highlight` outside the header.
// Arguments and result as ImGui::CollapsingHeader's.
[[nodiscard]] bool sectionHeader(const char *label, bool *open = nullptr, ImGuiTreeNodeFlags flags = 0);

// -- Row action button -------------------------------------------------------------------------------------------

// A small button for an action on the row it is drawn in: add, remove, revert, reorder, apply a preset. Its label is
// a verb or a name in sentence case ("Add", "Remove", "Revert", "Up", "Down", "Revert to prefab"). Returns whether it
// was clicked.
[[nodiscard]] bool rowButton(const char *label);

// -- Field widths ------------------------------------------------------------------------------------------------

enum class FieldWidth {
  Narrow,   // a short number: a time, a tile size (`narrowFieldWidth`)
  Standard, // a number, a name, a combo of short choices (`fieldWidth`)
  Wide,     // a filter or a longer text (`wideFieldWidth`)
};

// The width of the next item, from the theme's tokens in text heights (`ui::em`), so it follows the font and the
// display scale. A field that fills the row keeps ImGui::SetNextItemWidth(-1).
void setNextFieldWidth(FieldWidth width);

// -- Spacing -----------------------------------------------------------------------------------------------------

// The break between two sections of a panel: a separator with the theme's `sectionGap` above and below it. A menu's
// separator stays ImGui::Separator.
void sectionBreak();

// The theme's `groupGap` above a group that closes a section, such as its row of buttons.
void groupBreak();

// -- Full-bleed window -------------------------------------------------------------------------------------------

// ImGui::Begin for a window filled by an image (the Viewport and Game panels): no padding, and no scrolling, so the
// wheel is the view's. Like Begin, ImGui::End is called whatever this returned.
bool beginFullBleedWindow(const char *name, bool *open);

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

// A rectangle in screen space.
struct ScreenRect {
  ImVec2 min;
  ImVec2 max;
};

// A child that fits its contents over the theme's `overlay` backdrop and takes no input, for text drawn over the
// viewport. It grows to at most `maxSize` and cuts what does not fit, with no scrollbar. Like BeginChild,
// `endOverlay` is called whatever this returned.
bool beginOverlay(const char *id, ImVec2 maxSize);
void endOverlay();

// A line of text on the theme's `label` backdrop in the bottom-left corner of the area of `areaSize` at `areaMin` (in
// screen space), clipped to the area. Sizes follow the font. Returns the backdrop's rectangle, so an overlay drawn
// above it can stop short of it.
ScreenRect overlayLabel(ImDrawList &drawList, ImVec2 areaMin, ImVec2 areaSize, const char *text);

} // namespace sonnet::ui
