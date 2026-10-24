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

// ImLerp for colours, which every channel takes, alpha included, as Dear ImGui's own styles do.
ImVec4 lerp(const ImVec4 &a, const ImVec4 &b, float t) {
  return ImVec4{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
}

ImVec4 brighten(const ImVec4 &colour, float amount) {
  const auto up = [amount](float channel) { return channel + amount > 1.0f ? 1.0f : channel + amount; };
  return ImVec4{up(colour.x), up(colour.y), up(colour.z), colour.w};
}

// The colours, with the alphas and the lerps of Dear ImGui's dark style.
void applyColours(ImVec4 (&c)[ImGuiCol_COUNT], const Theme &t) {
  const ImVec4 clear{0.0f, 0.0f, 0.0f, 0.0f};
  c[ImGuiCol_Text] = t.text;
  c[ImGuiCol_TextDisabled] = t.textDisabled;
  c[ImGuiCol_WindowBg] = t.window;
  c[ImGuiCol_ChildBg] = clear;
  c[ImGuiCol_PopupBg] = t.popup;
  c[ImGuiCol_Border] = t.border;
  c[ImGuiCol_BorderShadow] = clear;
  c[ImGuiCol_FrameBg] = t.frame;
  c[ImGuiCol_FrameBgHovered] = withAlpha(t.accent, 0.40f);
  c[ImGuiCol_FrameBgActive] = withAlpha(t.accent, 0.67f);
  c[ImGuiCol_TitleBg] = t.title;
  c[ImGuiCol_TitleBgActive] = t.titleActive;
  c[ImGuiCol_TitleBgCollapsed] = t.titleCollapsed;
  c[ImGuiCol_MenuBarBg] = t.menuBar;
  c[ImGuiCol_ScrollbarBg] = t.scrollbarTrack;
  c[ImGuiCol_ScrollbarGrab] = t.scrollbarGrab;
  c[ImGuiCol_ScrollbarGrabHovered] = brighten(t.scrollbarGrab, 0.10f);
  c[ImGuiCol_ScrollbarGrabActive] = brighten(t.scrollbarGrab, 0.20f);
  c[ImGuiCol_CheckMark] = t.accent;
  c[ImGuiCol_CheckboxSelectedBg] = lerp(c[ImGuiCol_FrameBg], c[ImGuiCol_FrameBgHovered], 0.65f);
  c[ImGuiCol_SliderGrab] = t.accentGrab;
  c[ImGuiCol_SliderGrabActive] = t.accent;
  c[ImGuiCol_Button] = withAlpha(t.accent, 0.40f);
  c[ImGuiCol_ButtonHovered] = t.accent;
  c[ImGuiCol_ButtonActive] = t.accentPressed;
  c[ImGuiCol_Header] = withAlpha(t.accent, 0.31f);
  c[ImGuiCol_HeaderHovered] = withAlpha(t.accent, 0.80f);
  c[ImGuiCol_HeaderActive] = t.accent;
  c[ImGuiCol_Separator] = t.border;
  c[ImGuiCol_SeparatorHovered] = withAlpha(t.accentDark, 0.78f);
  c[ImGuiCol_SeparatorActive] = t.accentDark;
  c[ImGuiCol_ResizeGrip] = withAlpha(t.accent, 0.20f);
  c[ImGuiCol_ResizeGripHovered] = withAlpha(t.accent, 0.67f);
  c[ImGuiCol_ResizeGripActive] = withAlpha(t.accent, 0.95f);
  c[ImGuiCol_InputTextCursor] = t.text;
  c[ImGuiCol_TabHovered] = c[ImGuiCol_HeaderHovered];
  c[ImGuiCol_Tab] = lerp(c[ImGuiCol_Header], c[ImGuiCol_TitleBgActive], 0.80f);
  c[ImGuiCol_TabSelected] = lerp(c[ImGuiCol_HeaderActive], c[ImGuiCol_TitleBgActive], 0.60f);
  c[ImGuiCol_TabSelectedOverline] = c[ImGuiCol_HeaderActive];
  c[ImGuiCol_TabDimmed] = lerp(c[ImGuiCol_Tab], c[ImGuiCol_TitleBg], 0.80f);
  c[ImGuiCol_TabDimmedSelected] = lerp(c[ImGuiCol_TabSelected], c[ImGuiCol_TitleBg], 0.40f);
  c[ImGuiCol_TabDimmedSelectedOverline] = withAlpha(t.textDisabled, 0.0f);
  c[ImGuiCol_DockingPreview] = withAlpha(c[ImGuiCol_HeaderActive], c[ImGuiCol_HeaderActive].w * 0.7f);
  c[ImGuiCol_DockingEmptyBg] = t.dockEmpty;
  c[ImGuiCol_PlotLines] = t.plotLines;
  c[ImGuiCol_PlotLinesHovered] = t.plotLinesHover;
  c[ImGuiCol_PlotHistogram] = t.plotHistogram;
  c[ImGuiCol_PlotHistogramHovered] = t.plotHistogramHover;
  c[ImGuiCol_TableHeaderBg] = t.tableHeader;
  c[ImGuiCol_TableBorderStrong] = t.tableBorderStrong;
  c[ImGuiCol_TableBorderLight] = t.tableBorderLight;
  c[ImGuiCol_TableRowBg] = clear;
  c[ImGuiCol_TableRowBgAlt] = withAlpha(t.text, 0.06f);
  c[ImGuiCol_TextLink] = c[ImGuiCol_HeaderActive];
  c[ImGuiCol_TextSelectedBg] = withAlpha(t.accent, 0.35f);
  c[ImGuiCol_TreeLines] = c[ImGuiCol_Border];
  c[ImGuiCol_DragDropTarget] = t.dragDropTarget;
  c[ImGuiCol_DragDropTargetBg] = clear;
  c[ImGuiCol_UnsavedMarker] = t.text;
  c[ImGuiCol_NavCursor] = t.accent;
  c[ImGuiCol_NavWindowingHighlight] = withAlpha(t.text, 0.70f);
  c[ImGuiCol_NavWindowingDimBg] = withAlpha(t.dim, 0.20f);
  c[ImGuiCol_ModalWindowDimBg] = withAlpha(t.dim, 0.35f);
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
