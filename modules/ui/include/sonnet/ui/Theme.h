#pragma once

#include <imgui.h>

namespace sonnet::ui {

// A colour from 8-bit channels, which is how the editor's palette is usually written down.
constexpr ImVec4 rgba8(int r, int g, int b, int a = 255) {
  return ImVec4{static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f, static_cast<float>(b) / 255.0f,
                static_cast<float>(a) / 255.0f};
}

// The editor's look as unscaled base tokens (docs/editor.md, "The ui module"; ADR-0036). `applyTheme` derives an
// ImGuiStyle from them and a scale; the colours Dear ImGui has no slot for are read straight from `currentTheme()`.
// The defaults are the Graphite look (#391): neutral greys, one accent from the icon's blues, recessed and outlined
// fields, raised and outlined buttons.
struct Theme {
  // Colours as roles. `applyTheme` gives each ImGuiCol slot one of them.
  // Surfaces: the menu, tab and title bars; a panel; a popup, a menu or a modal.
  ImVec4 chrome{rgba8(0x18, 0x18, 0x1b)};
  ImVec4 window{rgba8(0x22, 0x22, 0x26)};
  ImVec4 popup{rgba8(0x2a, 0x2a, 0x2f)};
  // An input, a drag or a combo: recessed, darker than the panel.
  ImVec4 field{rgba8(0x19, 0x19, 0x1c)};
  ImVec4 fieldHovered{rgba8(0x1e, 0x1e, 0x22)};
  ImVec4 fieldActive{rgba8(0x14, 0x14, 0x17)};
  // A button: raised, lighter than the panel. Pressed is also a toggle button that is on.
  ImVec4 control{rgba8(0x30, 0x30, 0x36)};
  ImVec4 controlHovered{rgba8(0x3a, 0x3a, 0x41)};
  ImVec4 controlActive{rgba8(0x00, 0x57, 0xb0)};
  // The outline of fields, buttons and popups; the lines between rows, columns and panels.
  ImVec4 border{rgba8(0x6c, 0x6c, 0x75)};
  ImVec4 separator{rgba8(0x38, 0x38, 0x3e)};
  ImVec4 text{rgba8(0xe4, 0xe4, 0xe7)};
  ImVec4 textDisabled{rgba8(0xa6, 0xa6, 0xae)};
  // The accent marks state only: the focused tab, the checkmark, the slider grab, links and the nav cursor.
  ImVec4 accent{rgba8(0x3f, 0x9d, 0xe5)};
  ImVec4 check{rgba8(0x3f, 0x9d, 0xe5)};
  ImVec4 checkBox{rgba8(0x19, 0x19, 0x1c)};
  // The selected row of a list, a tree, a combo or a menu, and its hovered and pressed states.
  ImVec4 highlight{rgba8(0x11, 0x3c, 0x6b)};
  ImVec4 highlightHovered{rgba8(0x18, 0x31, 0x4d)};
  ImVec4 highlightActive{rgba8(0x00, 0x57, 0xb0)};
  ImVec4 scrollbarGrab{rgba8(0x46, 0x46, 0x4e)};
  // What a modal or the window switcher covers is dimmed with this.
  ImVec4 dim{0.0f, 0.0f, 0.0f, 0.45f};

  // What ImGuiCol has no slot for. Upstream declined application-defined slots, so these live here.
  ImVec4 danger{rgba8(0xf0, 0x7a, 0x72)};  // an error shown in a dialog
  ImVec4 warning{rgba8(0xf0, 0xb4, 0x4c)}; // a warning shown in a panel
  ImVec4 stop{rgba8(0xb3, 0x37, 0x2d)};    // the Stop button while playing, and a destructive dialog button
  ImVec4 paused{rgba8(0x8a, 0x5a, 0x00)};  // the Resume button while paused
  // The inspector's component headers: a flat grey band, which `InspectorPanel` pushes over the ImGuiCol_Header slots
  // (the selected rows keep `highlight`).
  ImVec4 sectionHeader{rgba8(0x2a, 0x2a, 0x2f)};
  ImVec4 sectionHeaderHovered{rgba8(0x33, 0x33, 0x39)};
  ImVec4 sectionHeaderActive{rgba8(0x3c, 0x3c, 0x43)};
  // What is selected: the 3D outline and the GUI element's rectangle.
  ImVec4 selection{1.00f, 0.60f, 0.10f, 1.00f};
  // A backdrop for text drawn over the viewport: the statistics overlay, and the view label's stronger one.
  ImVec4 overlay{0.0f, 0.0f, 0.0f, 0.62f};
  ImVec4 label{rgba8(0, 0, 0, 200)};
  ImVec4 labelText{rgba8(240, 240, 240)};
  // The X, Y and Z axes, shared by the gizmo and the viewport's axis widget, and each one's highlight.
  ImVec4 axisX{rgba8(225, 60, 60)};
  ImVec4 axisY{rgba8(70, 200, 70)};
  ImVec4 axisZ{rgba8(70, 110, 255)};
  ImVec4 axisXHighlight{rgba8(255, 130, 130)};
  ImVec4 axisYHighlight{rgba8(150, 255, 150)};
  ImVec4 axisZHighlight{rgba8(150, 170, 255)};
  ImVec4 gizmoOrigin{rgba8(230, 230, 230, 220)};
  // The axis widget: its disc, the filled ends' label, and the outlined negative ends.
  ImVec4 widgetDisc{rgba8(25, 28, 35, 130)};
  ImVec4 widgetDiscActive{rgba8(25, 28, 35, 190)};
  ImVec4 widgetLabel{rgba8(15, 18, 25)};
  ImVec4 widgetNegative{rgba8(35, 38, 45)};
  // The log panel's level colours. Trace, info, warn and error start as `textDisabled`, `text`, `warning` and `danger`.
  ImVec4 logTrace{textDisabled};
  ImVec4 logDebug{rgba8(0x8a, 0xb4, 0xf8)};
  ImVec4 logInfo{text};
  ImVec4 logWarn{warning};
  ImVec4 logError{danger};
  ImVec4 logCritical{rgba8(0xff, 0x6a, 0xd5)};

  // Sizes in unscaled pixels. `applyTheme` multiplies them by the scale.
  float textSize{14.0f}; // the text font; the monospace and icon fonts follow it
  ImVec2 windowPadding{8.0f, 8.0f};
  ImVec2 framePadding{8.0f, 4.0f};
  ImVec2 itemSpacing{8.0f, 4.0f};
  ImVec2 itemInnerSpacing{4.0f, 4.0f};
  ImVec2 cellPadding{4.0f, 2.0f};
  float indentSpacing{20.0f};
  float scrollbarSize{12.0f};
  float grabMinSize{12.0f};
  float windowRounding{0.0f};
  float childRounding{2.0f};
  float frameRounding{2.0f};
  float popupRounding{2.0f};
  float scrollbarRounding{12.0f}; // a pill: at least half the bar's width
  float grabRounding{2.0f};
  float tabRounding{2.0f};
  float windowBorderSize{0.0f};
  float childBorderSize{1.0f};
  float popupBorderSize{1.0f};
  float frameBorderSize{1.0f};
  float tabBorderSize{0.0f};
  float tabBarOverlineSize{2.0f}; // the line over the focused tab

  // Widths and distances the widgets of Widgets.h draw with. They are in no ImGuiStyle field, so the widgets multiply
  // them by the display scale themselves (`ui::scaled`, which reads `currentScale()`).
  float dialogButtonWidth{120.0f};     // a modal's buttons
  float dialogButtonWideWidth{200.0f}; // a modal's button with a label that is too long for the normal width
  float propertyLabelWidth{90.0f};     // the name column of the inspector's property tables
  float propertyRevertWidth{50.0f};    // the revert column of a script property
  float overlayInset{8.0f};            // from the corner of a view to an overlay drawn in it
};

// Overwrites `style` with the theme's, from the base values every time, so a change of scale never compounds the
// rounding of an earlier one. Colours at the base; the sizes of every field of ImGuiStyle times `scale`. With
// `viewports` on, the platform windows have no transparency: square corners and an opaque window background.
// The font scales (FontSizeBase, FontScaleMain, FontScaleDpi) are the application's and Dear ImGui's, and are kept.
void applyTheme(ImGuiStyle &style, const Theme &theme, float scale, bool viewports);

// The theme the panels read. A process has one: ImGuiLayer sets it, and without a layer (headless panel tests) it
// is the default Theme.
[[nodiscard]] const Theme &currentTheme();
void setCurrentTheme(const Theme &theme);

// The display scale the style was derived with, 1 for the base tokens. ImGuiLayer sets it where it calls `applyTheme`,
// and without a layer (headless panel tests) it is 1. The widgets multiply the width tokens that no ImGuiStyle field
// holds by it (`ui::scaled`); it is a ui value, not Dear ImGui's `ImGuiStyle::_MainScale`, which upstream marks as not
// for use.
[[nodiscard]] float currentScale();
void setCurrentScale(float scale);

// A theme colour as the packed value ImDrawList takes.
[[nodiscard]] inline ImU32 toU32(const ImVec4 &colour) {
  return ImGui::ColorConvertFloat4ToU32(colour);
}

} // namespace sonnet::ui
