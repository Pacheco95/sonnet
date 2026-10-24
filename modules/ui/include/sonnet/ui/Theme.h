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
// The defaults are Dear ImGui's dark style and the colours the editor drew as literals before there were tokens.
struct Theme {
  // Surfaces and text. Each ImGuiCol slot takes one of these, with the alpha ImGui's dark style gives it.
  ImVec4 text{1.00f, 1.00f, 1.00f, 1.00f};
  ImVec4 textDisabled{0.50f, 0.50f, 0.50f, 1.00f};
  ImVec4 window{0.06f, 0.06f, 0.06f, 0.94f};
  ImVec4 popup{0.08f, 0.08f, 0.08f, 0.94f};
  ImVec4 menuBar{0.14f, 0.14f, 0.14f, 1.00f};
  ImVec4 title{0.04f, 0.04f, 0.04f, 1.00f};
  ImVec4 titleActive{0.16f, 0.29f, 0.48f, 1.00f};
  ImVec4 titleCollapsed{0.00f, 0.00f, 0.00f, 0.51f};
  ImVec4 border{0.43f, 0.43f, 0.50f, 0.50f};
  ImVec4 frame{0.16f, 0.29f, 0.48f, 0.54f};
  ImVec4 dockEmpty{0.20f, 0.20f, 0.20f, 1.00f};
  ImVec4 scrollbarTrack{0.02f, 0.02f, 0.02f, 0.53f};
  ImVec4 scrollbarGrab{0.31f, 0.31f, 0.31f, 1.00f};
  // The accent: buttons, headers, tabs, checkmarks and the like, in three strengths.
  ImVec4 accent{0.26f, 0.59f, 0.98f, 1.00f};
  ImVec4 accentPressed{0.06f, 0.53f, 0.98f, 1.00f};
  ImVec4 accentGrab{0.24f, 0.52f, 0.88f, 1.00f};
  ImVec4 accentDark{0.10f, 0.40f, 0.75f, 1.00f};
  // Plots, tables and drag and drop.
  ImVec4 plotLines{0.61f, 0.61f, 0.61f, 1.00f};
  ImVec4 plotLinesHover{1.00f, 0.43f, 0.35f, 1.00f};
  ImVec4 plotHistogram{0.90f, 0.70f, 0.00f, 1.00f};
  ImVec4 plotHistogramHover{1.00f, 0.60f, 0.00f, 1.00f};
  ImVec4 tableHeader{0.19f, 0.19f, 0.20f, 1.00f};
  ImVec4 tableBorderStrong{0.31f, 0.31f, 0.35f, 1.00f};
  ImVec4 tableBorderLight{0.23f, 0.23f, 0.25f, 1.00f};
  ImVec4 dragDropTarget{1.00f, 1.00f, 0.00f, 0.90f};
  // The grey that dims what a modal or the window switcher covers; the alpha is the style's.
  ImVec4 dim{0.80f, 0.80f, 0.80f, 1.00f};

  // What ImGuiCol has no slot for. Upstream declined application-defined slots, so these live here.
  ImVec4 danger{0.95f, 0.40f, 0.40f, 1.00f};  // an error shown in a dialog
  ImVec4 warning{1.00f, 0.75f, 0.30f, 1.00f}; // a warning shown in a panel
  ImVec4 stop{0.70f, 0.25f, 0.20f, 1.00f};    // the Stop button while playing
  ImVec4 paused{0.75f, 0.55f, 0.15f, 1.00f};  // the Resume button while paused
  // What is selected: the 3D outline and the GUI element's rectangle.
  ImVec4 selection{1.00f, 0.60f, 0.10f, 1.00f};
  // A backdrop for text drawn over the viewport: the statistics overlay, and the view label's stronger one.
  ImVec4 overlay{0.00f, 0.00f, 0.00f, 0.60f};
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
  // The log panel's level colours.
  ImVec4 logTrace{0.55f, 0.55f, 0.55f, 1.0f};
  ImVec4 logDebug{0.60f, 0.75f, 0.95f, 1.0f};
  ImVec4 logInfo{0.90f, 0.90f, 0.90f, 1.0f};
  ImVec4 logWarn{0.95f, 0.80f, 0.30f, 1.0f};
  ImVec4 logError{0.95f, 0.40f, 0.40f, 1.0f};
  ImVec4 logCritical{1.00f, 0.30f, 0.80f, 1.0f};

  // Sizes in unscaled pixels. `applyTheme` multiplies them by the scale.
  float textSize{14.0f}; // the text font; the monospace and icon fonts follow it
  ImVec2 windowPadding{8.0f, 8.0f};
  ImVec2 framePadding{4.0f, 3.0f};
  ImVec2 itemSpacing{8.0f, 4.0f};
  ImVec2 itemInnerSpacing{4.0f, 4.0f};
  ImVec2 cellPadding{4.0f, 2.0f};
  float indentSpacing{21.0f};
  float scrollbarSize{14.0f};
  float grabMinSize{12.0f};
  float windowRounding{0.0f};
  float childRounding{0.0f};
  float frameRounding{0.0f};
  float popupRounding{0.0f};
  float scrollbarRounding{9.0f};
  float grabRounding{0.0f};
  float tabRounding{5.0f};
  float windowBorderSize{1.0f};
  float childBorderSize{1.0f};
  float popupBorderSize{1.0f};
  float frameBorderSize{0.0f};
  float tabBorderSize{0.0f};

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
