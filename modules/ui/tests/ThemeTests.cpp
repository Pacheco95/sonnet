#include <sonnet/ui/Theme.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <imgui.h>

using namespace sonnet;
using Catch::Approx;

namespace {

bool same(const ImVec4 &a, const ImVec4 &b) {
  return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}

bool near(const ImVec4 &a, const ImVec4 &b) {
  return a.x == Approx(b.x).margin(1e-6) && a.y == Approx(b.y).margin(1e-6) && a.z == Approx(b.z).margin(1e-6) &&
         a.w == Approx(b.w).margin(1e-6);
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
         a.FrameBorderSize == b.FrameBorderSize && a.DockingSeparatorSize == b.DockingSeparatorSize &&
         a.WindowMinSize.x == b.WindowMinSize.x && a._MainScale == b._MainScale;
}

bool sameColours(const ImGuiStyle &a, const ImGuiStyle &b) {
  for (int i = 0; i < ImGuiCol_COUNT; ++i) {
    if (!same(a.Colors[i], b.Colors[i])) {
      return false;
    }
  }
  return true;
}

} // namespace

TEST_CASE("the default theme at scale 1 is Dear ImGui's dark style", "[ui][theme]") {
  ImGuiStyle derived;
  ui::applyTheme(derived, ui::Theme{}, 1.0f, false);
  const ImGuiStyle dark; // its constructor applies StyleColorsDark

  for (int i = 0; i < ImGuiCol_COUNT; ++i) {
    INFO("ImGuiCol " << i << " " << ImGui::GetStyleColorName(i));
    CHECK(near(derived.Colors[i], dark.Colors[i]));
  }
  CHECK(sameSizes(derived, dark));
}

TEST_CASE("the style takes its colours and sizes from the tokens", "[ui][theme]") {
  ui::Theme theme;
  theme.text = {0.9f, 0.8f, 0.7f, 1.0f};
  theme.accent = {0.1f, 0.7f, 0.2f, 1.0f};
  theme.window = {0.2f, 0.2f, 0.3f, 0.5f};
  theme.framePadding = {6.0f, 5.0f};
  theme.frameRounding = 3.0f;
  theme.indentSpacing = 17.5f;

  ImGuiStyle style;
  ui::applyTheme(style, theme, 1.0f, false);

  CHECK(same(style.Colors[ImGuiCol_Text], theme.text));
  CHECK(same(style.Colors[ImGuiCol_InputTextCursor], theme.text));
  CHECK(same(style.Colors[ImGuiCol_WindowBg], theme.window));
  CHECK(same(style.Colors[ImGuiCol_ButtonHovered], theme.accent));
  CHECK(style.Colors[ImGuiCol_Button].y == theme.accent.y);
  CHECK(style.Colors[ImGuiCol_Button].w == Approx(0.40f));
  CHECK(style.FramePadding.x == 6.0f);
  CHECK(style.FramePadding.y == 5.0f);
  CHECK(style.FrameRounding == 3.0f);
  CHECK(style.IndentSpacing == 17.5f); // a fractional token is not truncated at scale 1
}

TEST_CASE("every colour slot is derived from a token", "[ui][theme]") {
  // With every colour token magenta, a slot that still has the dark style's value is one no token reaches.
  const ImVec4 magenta{1.0f, 0.0f, 1.0f, 1.0f};
  ui::Theme theme;
  for (ImVec4 *token : {&theme.text,
                        &theme.textDisabled,
                        &theme.window,
                        &theme.popup,
                        &theme.menuBar,
                        &theme.title,
                        &theme.titleActive,
                        &theme.titleCollapsed,
                        &theme.border,
                        &theme.frame,
                        &theme.dockEmpty,
                        &theme.scrollbarTrack,
                        &theme.scrollbarGrab,
                        &theme.accent,
                        &theme.accentPressed,
                        &theme.accentGrab,
                        &theme.accentDark,
                        &theme.plotLines,
                        &theme.plotLinesHover,
                        &theme.plotHistogram,
                        &theme.plotHistogramHover,
                        &theme.tableHeader,
                        &theme.tableBorderStrong,
                        &theme.tableBorderLight,
                        &theme.dragDropTarget,
                        &theme.dim}) {
    *token = magenta;
  }

  ImGuiStyle style;
  ui::applyTheme(style, theme, 1.0f, false);
  for (int i = 0; i < ImGuiCol_COUNT; ++i) {
    INFO("ImGuiCol " << i << " " << ImGui::GetStyleColorName(i));
    const ImVec4 &colour = style.Colors[i];
    const bool clear = colour.x == 0.0f && colour.y == 0.0f && colour.z == 0.0f && colour.w == 0.0f;
    // The hovered and active scrollbar grabs brighten their token, so they are lighter than pure magenta.
    const bool brightened = i == ImGuiCol_ScrollbarGrabHovered || i == ImGuiCol_ScrollbarGrabActive;
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
  CHECK(style.WindowBorderSize == 2.0f);
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

TEST_CASE("colours pack as the byte literals they replaced", "[ui][theme]") {
  const ui::Theme theme;
  CHECK(ui::toU32(theme.selection) == IM_COL32(255, 153, 26, 255));
  CHECK(ui::toU32(theme.axisX) == IM_COL32(225, 60, 60, 255));
  CHECK(ui::toU32(theme.axisZHighlight) == IM_COL32(150, 170, 255, 255));
  CHECK(ui::toU32(theme.gizmoOrigin) == IM_COL32(230, 230, 230, 220));
  CHECK(ui::toU32(theme.label) == IM_COL32(0, 0, 0, 200));
}
