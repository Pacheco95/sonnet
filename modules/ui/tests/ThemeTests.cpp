#include <sonnet/ui/Theme.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <string_view>
#include <utility>
#include <vector>

using namespace sonnet;
using Catch::Approx;

namespace {

bool same(const ImVec4 &a, const ImVec4 &b) {
  return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}

// The sizes a theme sets, which are what a scale has to carry.
bool sameSizes(const ImGuiStyle &a, const ImGuiStyle &b) {
  const auto same2 = [](ImVec2 u, ImVec2 v) { return u.x == v.x && u.y == v.y; };
  return same2(a.WindowPadding, b.WindowPadding) && same2(a.FramePadding, b.FramePadding) &&
         same2(a.ItemSpacing, b.ItemSpacing) && same2(a.ItemInnerSpacing, b.ItemInnerSpacing) &&
         same2(a.CellPadding, b.CellPadding) && a.IndentSpacing == b.IndentSpacing &&
         a.ScrollbarSize == b.ScrollbarSize && a.GrabMinSize == b.GrabMinSize && a.WindowRounding == b.WindowRounding &&
         a.FrameRounding == b.FrameRounding && a.ScrollbarRounding == b.ScrollbarRounding &&
         a.TabRounding == b.TabRounding && a.WindowBorderSize == b.WindowBorderSize &&
         a.FrameBorderSize == b.FrameBorderSize && a.TabBarOverlineSize == b.TabBarOverlineSize &&
         a.DockingSeparatorSize == b.DockingSeparatorSize && a.WindowMinSize.x == b.WindowMinSize.x &&
         a._MainScale == b._MainScale;
}

bool sameColours(const ImGuiStyle &a, const ImGuiStyle &b) {
  for (int i = 0; i < ImGuiCol_COUNT; ++i) {
    if (!same(a.Colors[i], b.Colors[i])) {
      return false;
    }
  }
  return true;
}

// WCAG 2's relative luminance of an sRGB colour.
double luminance(const ImVec4 &colour) {
  const auto linear = [](float channel) {
    const double v = static_cast<double>(channel);
    return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
  };
  return 0.2126 * linear(colour.x) + 0.7152 * linear(colour.y) + 0.0722 * linear(colour.z);
}

double contrast(const ImVec4 &a, const ImVec4 &b) {
  const double la = luminance(a);
  const double lb = luminance(b);
  return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

// `top` drawn over the opaque `under`, blended per channel in sRGB with straight alpha, as the editor's ImGui pass
// does (a capture of a translucent slot lands on these bytes). The result is opaque.
ImVec4 over(const ImVec4 &top, const ImVec4 &under) {
  const float a = top.w;
  return ImVec4{top.x * a + under.x * (1.0f - a), top.y * a + under.y * (1.0f - a), top.z * a + under.z * (1.0f - a),
                1.0f};
}

ImVec4 faded(const ImVec4 &colour, float alpha) {
  return ImVec4{colour.x, colour.y, colour.z, colour.w * alpha};
}

struct Pair {
  const char *what;
  ImVec4 foreground;
  ImVec4 background;
  double minimum;
};

// Every pair docs/editor.md ("Theme") holds the look to, on the style `applyTheme` derives from `theme`.
std::vector<Pair> contrastPairs(const ui::Theme &theme) {
  ImGuiStyle style;
  ui::applyTheme(style, theme, 1.0f, false);
  const ImVec4(&c)[ImGuiCol_COUNT] = style.Colors;
  // The window clears to black, so even a translucent panel is measured over something opaque.
  const ImVec4 panel = over(c[ImGuiCol_WindowBg], ImVec4{0.0f, 0.0f, 0.0f, 1.0f});
  const ImVec4 popup = over(c[ImGuiCol_PopupBg], panel);
  const ImVec4 field = over(c[ImGuiCol_FrameBg], panel);
  const auto on = [&](ImGuiCol slot) { return over(c[slot], panel); };
  const ImVec4 text = c[ImGuiCol_Text];
  const ImVec4 hint = c[ImGuiCol_TextDisabled];
  constexpr double Text = 4.5;
  constexpr double Graphic = 3.0;

  std::vector<Pair> pairs;
  // Text on every surface it is drawn on, in every state.
  for (const auto &[what, slot] : std::initializer_list<std::pair<const char *, ImGuiCol>>{
           {"menu bar", ImGuiCol_MenuBarBg},
           {"title bar", ImGuiCol_TitleBg},
           {"focused title bar", ImGuiCol_TitleBgActive},
           {"tab", ImGuiCol_Tab},
           {"hovered tab", ImGuiCol_TabHovered},
           {"selected tab", ImGuiCol_TabSelected},
           {"unfocused tab", ImGuiCol_TabDimmed},
           {"unfocused selected tab", ImGuiCol_TabDimmedSelected},
           {"panel", ImGuiCol_WindowBg},
           {"field", ImGuiCol_FrameBg},
           {"hovered field", ImGuiCol_FrameBgHovered},
           {"pressed field", ImGuiCol_FrameBgActive},
           {"button", ImGuiCol_Button},
           {"hovered button", ImGuiCol_ButtonHovered},
           {"pressed or toggled button", ImGuiCol_ButtonActive},
           {"selected row", ImGuiCol_Header},
           {"hovered selected row", ImGuiCol_HeaderHovered},
           {"pressed row", ImGuiCol_HeaderActive},
           {"table header", ImGuiCol_TableHeaderBg},
           {"alternate table row", ImGuiCol_TableRowBgAlt},
       }) {
    pairs.push_back({what, text, on(slot), Text});
  }
  pairs.push_back({"popup", text, popup, Text});
  pairs.push_back({"selected text in a field", text, over(c[ImGuiCol_TextSelectedBg], field), Text});
  pairs.push_back({"component header", text, over(theme.sectionHeader, panel), Text});
  pairs.push_back({"hovered component header", text, over(theme.sectionHeaderHovered, panel), Text});
  pairs.push_back({"pressed component header", text, over(theme.sectionHeaderActive, panel), Text});
  pairs.push_back({"Stop", text, over(theme.stop, on(ImGuiCol_MenuBarBg)), Text});
  pairs.push_back({"Resume", text, over(theme.paused, on(ImGuiCol_MenuBarBg)), Text});
  pairs.push_back({"destructive dialog button", text, over(theme.stop, popup), Text});
  // Disabled text carries information: input hints, the Assets Source column, disabled entities.
  pairs.push_back({"disabled text on the panel", hint, panel, Text});
  pairs.push_back({"disabled text in a popup", hint, popup, Text});
  pairs.push_back({"disabled text in a field", hint, field, Text});
  pairs.push_back({"disabled text on a selected row", hint, on(ImGuiCol_Header), Text});
  // Coloured text.
  for (const auto &[what, colour] : std::initializer_list<std::pair<const char *, ImVec4>>{
           {"trace", theme.logTrace},
           {"debug", theme.logDebug},
           {"info", theme.logInfo},
           {"warn", theme.logWarn},
           {"error", theme.logError},
           {"critical", theme.logCritical},
           {"warning in the inspector", theme.warning},
           {"link", c[ImGuiCol_TextLink]},
       }) {
    pairs.push_back({what, colour, panel, Text});
  }
  pairs.push_back({"danger in a dialog", theme.danger, popup, Text});
  // Text in a disabled control: BeginDisabled multiplies every alpha, the control's too.
  const float disabled = style.DisabledAlpha;
  const ImVec4 disabledButton = over(faded(c[ImGuiCol_Button], disabled), panel);
  const ImVec4 disabledField = over(faded(c[ImGuiCol_FrameBg], disabled), panel);
  pairs.push_back({"text in a disabled button", over(faded(text, disabled), disabledButton), disabledButton, Graphic});
  pairs.push_back({"text in a disabled field", over(faded(text, disabled), disabledField), disabledField, Graphic});
  // What marks out a control against the panel: its border, or its fill when frames have none.
  const auto outline = [&](ImGuiCol fill) { return style.FrameBorderSize > 0.0f ? on(ImGuiCol_Border) : on(fill); };
  pairs.push_back({"a field's outline", outline(ImGuiCol_FrameBg), panel, Graphic});
  pairs.push_back({"a button's outline", outline(ImGuiCol_Button), panel, Graphic});
  // The marks of state.
  pairs.push_back({"checkmark", c[ImGuiCol_CheckMark], over(c[ImGuiCol_CheckboxSelectedBg], panel), Graphic});
  pairs.push_back({"slider grab", c[ImGuiCol_SliderGrab], field, Graphic});
  pairs.push_back({"focused tab line on its tab", c[ImGuiCol_TabSelectedOverline], on(ImGuiCol_TabSelected), Graphic});
  pairs.push_back({"focused tab line on the tab bar", c[ImGuiCol_TabSelectedOverline], on(ImGuiCol_Tab), Graphic});
  pairs.push_back({"nav cursor", c[ImGuiCol_NavCursor], panel, Graphic});
  return pairs;
}

} // namespace

TEST_CASE("the default theme meets the contrast it is held to", "[ui][theme]") {
  for (const Pair &pair : contrastPairs(ui::Theme{})) {
    INFO(pair.what << ": " << contrast(pair.foreground, pair.background) << ":1, at least " << pair.minimum);
    CHECK(contrast(pair.foreground, pair.background) >= pair.minimum);
  }
}

TEST_CASE("the contrast check finds a pair below its threshold", "[ui][theme]") {
  // Dark grey hints on the panel and a border close to it are what the check exists to catch.
  ui::Theme theme;
  theme.textDisabled = theme.window;
  theme.border = theme.window;
  std::vector<std::string_view> failed;
  for (const Pair &pair : contrastPairs(theme)) {
    if (contrast(pair.foreground, pair.background) < pair.minimum) {
      failed.push_back(pair.what);
    }
  }
  CHECK(std::ranges::find(failed, "disabled text on the panel") != failed.end());
  CHECK(std::ranges::find(failed, "a field's outline") != failed.end());
  CHECK(std::ranges::find(failed, "button") == failed.end());
  // WCAG's own extremes: black on white is 21:1, and a colour on itself 1:1.
  CHECK(contrast({0.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}) == Approx(21.0));
  CHECK(contrast(theme.window, theme.window) == Approx(1.0));
}

TEST_CASE("the default theme's sizes are on the 4 px grid", "[ui][theme]") {
  const ui::Theme theme;
  const auto onGrid = [](float value) { return std::fmod(value, 4.0f) == 0.0f || value == 2.0f; };
  for (const float spacing :
       {theme.windowPadding.x, theme.windowPadding.y, theme.framePadding.x, theme.framePadding.y, theme.itemSpacing.x,
        theme.itemSpacing.y, theme.itemInnerSpacing.x, theme.itemInnerSpacing.y, theme.cellPadding.x,
        theme.cellPadding.y, theme.indentSpacing}) {
    CHECK(onGrid(spacing));
  }
  for (const float radius : {theme.windowRounding, theme.childRounding, theme.frameRounding, theme.popupRounding,
                             theme.grabRounding, theme.tabRounding}) {
    CHECK((radius == 0.0f || radius == 2.0f || radius == 4.0f));
  }
  CHECK(theme.scrollbarRounding >= theme.scrollbarSize * 0.5f); // a pill
  for (const float border : {theme.windowBorderSize, theme.childBorderSize, theme.popupBorderSize,
                             theme.frameBorderSize, theme.tabBorderSize, theme.tabBarOverlineSize}) {
    CHECK((border == 0.0f || border == 1.0f || border == 2.0f));
  }
  // The icon offset and the layouts' em floors were measured at 14 px.
  CHECK(theme.textSize == 14.0f);

  ImGuiStyle derived;
  ui::applyTheme(derived, theme, 1.0f, false);
  CHECK(derived.FramePadding.y == 4.0f);
  CHECK(derived.TabBarOverlineSize == 2.0f);
  // What the theme leaves alone keeps Dear ImGui's default.
  CHECK(derived.DockingSeparatorSize == 2.0f);
  CHECK(derived.WindowMinSize.x == 32.0f);
}

TEST_CASE("the style takes its colours and sizes from the tokens", "[ui][theme]") {
  ui::Theme theme;
  theme.text = {0.9f, 0.8f, 0.7f, 1.0f};
  theme.accent = {0.1f, 0.7f, 0.2f, 1.0f};
  theme.control = {0.4f, 0.3f, 0.2f, 1.0f};
  theme.window = {0.2f, 0.2f, 0.3f, 0.5f};
  theme.framePadding = {6.0f, 5.0f};
  theme.frameRounding = 3.0f;
  theme.indentSpacing = 17.5f;

  ImGuiStyle style;
  ui::applyTheme(style, theme, 1.0f, false);

  CHECK(same(style.Colors[ImGuiCol_Text], theme.text));
  CHECK(same(style.Colors[ImGuiCol_InputTextCursor], theme.text));
  CHECK(same(style.Colors[ImGuiCol_WindowBg], theme.window));
  CHECK(same(style.Colors[ImGuiCol_TabSelected], theme.window));
  CHECK(same(style.Colors[ImGuiCol_Button], theme.control));
  CHECK(same(style.Colors[ImGuiCol_TableHeaderBg], theme.control));
  for (const ImGuiCol slot : {ImGuiCol_SliderGrab, ImGuiCol_TabSelectedOverline, ImGuiCol_TextLink, ImGuiCol_NavCursor,
                              ImGuiCol_SeparatorActive}) {
    CHECK(same(style.Colors[slot], theme.accent));
  }
  CHECK(style.Colors[ImGuiCol_DockingPreview].y == theme.accent.y);
  CHECK(style.Colors[ImGuiCol_DockingPreview].w == Approx(0.40f));
  CHECK(style.FramePadding.x == 6.0f);
  CHECK(style.FramePadding.y == 5.0f);
  CHECK(style.FrameRounding == 3.0f);
  CHECK(style.IndentSpacing == 17.5f); // a fractional token is not truncated at scale 1
}

TEST_CASE("every colour slot is derived from a token", "[ui][theme]") {
  // With every colour token magenta, a slot that still has the dark style's value is one no token reaches.
  const ImVec4 magenta{1.0f, 0.0f, 1.0f, 1.0f};
  ui::Theme theme;
  for (ImVec4 *token : {&theme.chrome,        &theme.window,           &theme.popup,           &theme.field,
                        &theme.fieldHovered,  &theme.fieldActive,      &theme.control,         &theme.controlHovered,
                        &theme.controlActive, &theme.border,           &theme.separator,       &theme.text,
                        &theme.textDisabled,  &theme.accent,           &theme.check,           &theme.checkBox,
                        &theme.highlight,     &theme.highlightHovered, &theme.highlightActive, &theme.scrollbarGrab,
                        &theme.dim,           &theme.warning}) {
    *token = magenta;
  }

  ImGuiStyle style;
  ui::applyTheme(style, theme, 1.0f, false);
  for (int i = 0; i < ImGuiCol_COUNT; ++i) {
    INFO("ImGuiCol " << i << " " << ImGui::GetStyleColorName(i));
    const ImVec4 &colour = style.Colors[i];
    const bool clear = colour.x == 0.0f && colour.y == 0.0f && colour.z == 0.0f && colour.w == 0.0f;
    // The pressed and hovered grabs brighten their token, so they are lighter than pure magenta.
    const bool brightened =
        i == ImGuiCol_ScrollbarGrabHovered || i == ImGuiCol_ScrollbarGrabActive || i == ImGuiCol_SliderGrabActive;
    const bool magentaTint = colour.x == Approx(1.0f) && colour.y == Approx(0.0f) && colour.z == Approx(1.0f);
    CHECK((clear || brightened || magentaTint));
  }
}

TEST_CASE("a style at scale 2 and then at 1 is exactly the style at scale 1", "[ui][theme]") {
  ui::Theme theme;
  theme.framePadding = {5.0f, 3.0f};
  theme.itemSpacing = {7.0f, 5.0f};
  theme.indentSpacing = 21.0f;

  ImGuiStyle reference;
  ui::applyTheme(reference, theme, 1.0f, true);

  ImGuiStyle style;
  ui::applyTheme(style, theme, 2.0f, true);
  CHECK(style.FramePadding.x == 10.0f);
  CHECK(style.ItemSpacing.y == 10.0f);
  CHECK(style.IndentSpacing == 42.0f);
  CHECK(style.FrameBorderSize == 2.0f);
  CHECK(!sameSizes(style, reference));
  // Colours are not sizes: a scale leaves them alone.
  CHECK(sameColours(style, reference));

  ui::applyTheme(style, theme, 1.0f, true);
  CHECK(sameSizes(style, reference));
  CHECK(sameColours(style, reference));

  // The rounding of 1.5 at scale 1.25 would drift if a scaled style were scaled again.
  ui::applyTheme(style, theme, 1.25f, true);
  ui::applyTheme(style, theme, 1.5f, true);
  ImGuiStyle direct;
  ui::applyTheme(direct, theme, 1.5f, true);
  CHECK(sameSizes(style, direct));
}

TEST_CASE("with viewports on the window corners are square and its background opaque", "[ui][theme]") {
  ui::Theme theme;
  theme.windowRounding = 6.0f;
  theme.window = {0.1f, 0.1f, 0.1f, 0.8f};

  ImGuiStyle plain;
  ui::applyTheme(plain, theme, 1.0f, false);
  CHECK(plain.WindowRounding == 6.0f);
  CHECK(plain.Colors[ImGuiCol_WindowBg].w == Approx(0.8f));

  ImGuiStyle viewports;
  ui::applyTheme(viewports, theme, 1.0f, true);
  CHECK(viewports.WindowRounding == 0.0f);
  CHECK(viewports.Colors[ImGuiCol_WindowBg].w == 1.0f);

  ui::applyTheme(viewports, theme, 2.0f, true);
  CHECK(viewports.WindowRounding == 0.0f);
  CHECK(viewports.Colors[ImGuiCol_WindowBg].w == 1.0f);
}

TEST_CASE("applying a theme keeps the font scales", "[ui][theme]") {
  ImGuiStyle style;
  style.FontSizeBase = 14.0f;
  style.FontScaleMain = 1.25f;
  style.FontScaleDpi = 2.0f;
  ui::applyTheme(style, ui::Theme{}, 2.0f, false);
  CHECK(style.FontSizeBase == 14.0f);
  CHECK(style.FontScaleMain == 1.25f);
  CHECK(style.FontScaleDpi == 2.0f);
}

TEST_CASE("the current theme is the default until one is set", "[ui][theme]") {
  CHECK(same(ui::currentTheme().danger, ui::Theme{}.danger));

  ui::Theme red;
  red.danger = {1.0f, 0.0f, 0.0f, 1.0f};
  ui::setCurrentTheme(red);
  CHECK(same(ui::currentTheme().danger, red.danger));
  ui::setCurrentTheme(ui::Theme{});
  CHECK(same(ui::currentTheme().danger, ui::Theme{}.danger));
}

TEST_CASE("the current scale is 1 until one is set", "[ui][theme]") {
  CHECK(ui::currentScale() == 1.0f);
  ui::setCurrentScale(1.5f);
  CHECK(ui::currentScale() == 1.5f);
  ui::setCurrentScale(1.0f);
  CHECK(ui::currentScale() == 1.0f);
}

TEST_CASE("colours pack as the byte literals they replaced", "[ui][theme]") {
  const ui::Theme theme;
  CHECK(ui::toU32(theme.selection) == IM_COL32(255, 153, 26, 255));
  CHECK(ui::toU32(theme.axisX) == IM_COL32(225, 60, 60, 255));
  CHECK(ui::toU32(theme.axisZHighlight) == IM_COL32(150, 170, 255, 255));
  CHECK(ui::toU32(theme.gizmoOrigin) == IM_COL32(230, 230, 230, 220));
  CHECK(ui::toU32(theme.label) == IM_COL32(0, 0, 0, 200));
}
