#include <sonnet/ui/Theme.h>

#include <sonnet/core/Assert.h>

namespace sonnet::ui {

namespace {

// Every ImGuiCol slot is set below. A slot added by a newer Dear ImGui would otherwise keep the dark style's value
// without a token behind it; this stops the build until it gets one.
static_assert(ImGuiCol_COUNT == 63, "a new ImGuiCol slot needs a token and a line in applyTheme");

ImVec4 withAlpha(const ImVec4 &colour, float alpha) {
  return ImVec4{colour.x, colour.y, colour.z, alpha};
}

ImVec4 brighten(const ImVec4 &colour, float amount) {
  const auto up = [amount](float channel) { return channel + amount > 1.0f ? 1.0f : channel + amount; };
  return ImVec4{up(colour.x), up(colour.y), up(colour.z), colour.w};
}

// Each slot takes one role. Only the drag and resize feedback, the scrollbar's and the slider's pressed states and a
// few tints derive from theirs, never from another slot.
void applyColours(ImVec4 (&c)[ImGuiCol_COUNT], const Theme &t) {
  const ImVec4 clear{0.0f, 0.0f, 0.0f, 0.0f};
  c[ImGuiCol_Text] = t.text;
  c[ImGuiCol_TextDisabled] = t.textDisabled;
  c[ImGuiCol_WindowBg] = t.window;
  c[ImGuiCol_ChildBg] = clear;
  c[ImGuiCol_PopupBg] = t.popup;
  c[ImGuiCol_Border] = t.border;
  c[ImGuiCol_BorderShadow] = clear;
  c[ImGuiCol_FrameBg] = t.field;
  c[ImGuiCol_FrameBgHovered] = t.fieldHovered;
  c[ImGuiCol_FrameBgActive] = t.fieldActive;
  c[ImGuiCol_TitleBg] = t.chrome;
  c[ImGuiCol_TitleBgActive] = t.chrome;
  c[ImGuiCol_TitleBgCollapsed] = t.chrome;
  c[ImGuiCol_MenuBarBg] = t.chrome;
  c[ImGuiCol_ScrollbarBg] = clear;
  c[ImGuiCol_ScrollbarGrab] = t.scrollbarGrab;
  c[ImGuiCol_ScrollbarGrabHovered] = brighten(t.scrollbarGrab, 0.08f);
  c[ImGuiCol_ScrollbarGrabActive] = brighten(t.scrollbarGrab, 0.16f);
  c[ImGuiCol_CheckMark] = t.check;
  c[ImGuiCol_CheckboxSelectedBg] = t.checkBox;
  c[ImGuiCol_SliderGrab] = t.accent;
  c[ImGuiCol_SliderGrabActive] = brighten(t.accent, 0.10f);
  c[ImGuiCol_Button] = t.control;
  c[ImGuiCol_ButtonHovered] = t.controlHovered;
  c[ImGuiCol_ButtonActive] = t.controlActive;
  c[ImGuiCol_Header] = t.highlight;
  c[ImGuiCol_HeaderHovered] = t.highlightHovered;
  c[ImGuiCol_HeaderActive] = t.highlightActive;
  c[ImGuiCol_Separator] = t.separator;
  c[ImGuiCol_SeparatorHovered] = withAlpha(t.accent, 0.60f);
  c[ImGuiCol_SeparatorActive] = t.accent;
  c[ImGuiCol_ResizeGrip] = clear;
  c[ImGuiCol_ResizeGripHovered] = withAlpha(t.accent, 0.60f);
  c[ImGuiCol_ResizeGripActive] = t.accent;
  c[ImGuiCol_InputTextCursor] = t.text;
  c[ImGuiCol_TabHovered] = t.controlHovered;
  c[ImGuiCol_Tab] = t.chrome;
  c[ImGuiCol_TabSelected] = t.window;
  c[ImGuiCol_TabSelectedOverline] = t.accent;
  c[ImGuiCol_TabDimmed] = t.chrome;
  c[ImGuiCol_TabDimmedSelected] = t.window;
  c[ImGuiCol_TabDimmedSelectedOverline] = clear;
  c[ImGuiCol_DockingPreview] = withAlpha(t.accent, 0.40f);
  c[ImGuiCol_DockingEmptyBg] = t.chrome;
  c[ImGuiCol_PlotLines] = t.accent;
  c[ImGuiCol_PlotLinesHovered] = t.warning;
  c[ImGuiCol_PlotHistogram] = t.accent;
  c[ImGuiCol_PlotHistogramHovered] = t.warning;
  c[ImGuiCol_TableHeaderBg] = t.control;
  c[ImGuiCol_TableBorderStrong] = t.separator;
  c[ImGuiCol_TableBorderLight] = t.separator;
  c[ImGuiCol_TableRowBg] = clear;
  c[ImGuiCol_TableRowBgAlt] = withAlpha(t.text, 0.03f);
  c[ImGuiCol_TextLink] = t.accent;
  c[ImGuiCol_TextSelectedBg] = withAlpha(t.accent, 0.35f);
  c[ImGuiCol_TreeLines] = t.separator;
  c[ImGuiCol_DragDropTarget] = t.accent;
  c[ImGuiCol_DragDropTargetBg] = clear;
  c[ImGuiCol_UnsavedMarker] = t.text;
  c[ImGuiCol_NavCursor] = t.accent;
  c[ImGuiCol_NavWindowingHighlight] = withAlpha(t.text, 0.70f);
  c[ImGuiCol_NavWindowingDimBg] = withAlpha(t.dim, t.dim.w * 0.6f);
  c[ImGuiCol_ModalWindowDimBg] = t.dim;
}

Theme g_theme;
float g_scale = 1.0f;

} // namespace

void applyTheme(ImGuiStyle &style, const Theme &theme, float scale, bool viewports) {
  SONNET_ASSERT(scale > 0.0f, "a style scale has to be positive");
  const float fontSizeBase = style.FontSizeBase;
  const float fontScaleMain = style.FontScaleMain;
  const float fontScaleDpi = style.FontScaleDpi;

  style = ImGuiStyle{};
  style.WindowPadding = theme.windowPadding;
  style.FramePadding = theme.framePadding;
  style.ItemSpacing = theme.itemSpacing;
  style.ItemInnerSpacing = theme.itemInnerSpacing;
  style.CellPadding = theme.cellPadding;
  style.IndentSpacing = theme.indentSpacing;
  style.ScrollbarSize = theme.scrollbarSize;
  style.GrabMinSize = theme.grabMinSize;
  style.WindowRounding = theme.windowRounding;
  style.ChildRounding = theme.childRounding;
  style.FrameRounding = theme.frameRounding;
  style.PopupRounding = theme.popupRounding;
  style.ScrollbarRounding = theme.scrollbarRounding;
  style.GrabRounding = theme.grabRounding;
  style.TabRounding = theme.tabRounding;
  style.WindowBorderSize = theme.windowBorderSize;
  style.ChildBorderSize = theme.childBorderSize;
  style.PopupBorderSize = theme.popupBorderSize;
  style.FrameBorderSize = theme.frameBorderSize;
  style.TabBorderSize = theme.tabBorderSize;
  style.TabBarOverlineSize = theme.tabBarOverlineSize;
  applyColours(style.Colors, theme);
  if (viewports) {
    // Platform windows have no transparency: an opaque background and square corners.
    style.WindowRounding = 0.0f;
    style.Colors[ImGuiCol_WindowBg].w = 1.0f;
  }
  // ScaleAllSizes truncates to whole pixels, which is why it runs once, over the base values just set. At scale 1 it
  // would only truncate fractional tokens, so it is skipped.
  if (scale != 1.0f) {
    style.ScaleAllSizes(scale);
  }

  style.FontSizeBase = fontSizeBase;
  style.FontScaleMain = fontScaleMain;
  style.FontScaleDpi = fontScaleDpi;
}

const Theme &currentTheme() {
  return g_theme;
}

void setCurrentTheme(const Theme &theme) {
  g_theme = theme;
}

float currentScale() {
  return g_scale;
}

void setCurrentScale(float scale) {
  SONNET_ASSERT(scale > 0.0f, "a display scale has to be positive");
  g_scale = scale;
}

} // namespace sonnet::ui
