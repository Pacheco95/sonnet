#include <sonnet/ui/Theme.h>
#include <sonnet/ui/Widgets.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <functional>
#include <string>

using namespace sonnet;
using Catch::Approx;

namespace {

// A Dear ImGui context without a renderer or a window: enough to lay out widgets and feed them input. The style is the
// theme's at `scale`, as the layer would apply it.
class HeadlessImGui {
public:
  explicit HeadlessImGui(float scale = 1.0f, const ui::Theme &theme = ui::Theme{}) {
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2{800.0f, 600.0f};
    io.IniFilename = nullptr;
    unsigned char *pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ui::setCurrentTheme(theme);
    ui::applyTheme(ImGui::GetStyle(), theme, scale, false);
    ui::setCurrentScale(scale);
  }
  ~HeadlessImGui() {
    ui::setCurrentTheme(ui::Theme{});
    ui::setCurrentScale(1.0f);
    ImGui::DestroyContext();
  }
  HeadlessImGui(const HeadlessImGui &) = delete;
  HeadlessImGui &operator=(const HeadlessImGui &) = delete;

  // One frame with `body` inside a window at the origin that does not move or show a title.
  static void frame(const std::function<void()> &body) {
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2{0.0f, 0.0f});
    ImGui::SetNextWindowSize(ImVec2{700.0f, 400.0f});
    ImGui::Begin("host", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar);
    body();
    ImGui::End();
    ImGui::Render();
  }

  // A click at `position`: the pointer moves, goes down and comes up, with a frame between each as a user's takes
  // (the input queue trickles one kind of event per frame). Every frame runs `body`.
  static void click(ImVec2 position, const std::function<void()> &body) {
    ImGuiIO &io = ImGui::GetIO();
    io.AddMousePosEvent(position.x, position.y);
    frame(body);
    frame(body);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    frame(body);
    frame(body);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    frame(body);
    frame(body);
  }

  static void press(ImGuiKey key, const std::function<void()> &body) {
    ImGuiIO &io = ImGui::GetIO();
    io.AddKeyEvent(key, true);
    frame(body);
    io.AddKeyEvent(key, false);
    frame(body);
  }
};

// Whether a vertex of the last frame's draw lists has this colour: how a test sees what colour was drawn.
bool drewColour(ImU32 colour) {
  const ImDrawData *data = ImGui::GetDrawData();
  for (const ImDrawList *list : data->CmdLists) {
    for (const ImDrawVert &vertex : list->VtxBuffer) {
      if (vertex.col == colour) {
        return true;
      }
    }
  }
  return false;
}

// A theme whose tokens cannot be mistaken for each other or for Dear ImGui's own colours.
ui::Theme loudTheme() {
  ui::Theme theme;
  theme.stop = {0.9f, 0.1f, 0.8f, 1.0f};
  theme.paused = {0.1f, 0.9f, 0.7f, 1.0f};
  theme.danger = {0.2f, 0.9f, 0.1f, 1.0f};
  theme.warning = {0.9f, 0.9f, 0.1f, 1.0f};
  theme.overlay = {0.3f, 0.2f, 0.9f, 0.5f};
  theme.label = {0.9f, 0.5f, 0.2f, 1.0f};
  theme.labelText = {0.1f, 0.3f, 0.6f, 1.0f};
  return theme;
}

// The middle of the `index`th button of a row of buttons `width` wide, in the host window.
ImVec2 buttonCentre(int index, float width) {
  const ImGuiStyle &style = ImGui::GetStyle();
  return {style.WindowPadding.x + static_cast<float>(index) * (width + style.ItemSpacing.x) + width * 0.5f,
          style.WindowPadding.y + (ImGui::GetFontSize() + style.FramePadding.y * 2.0f) * 0.5f};
}

} // namespace

TEST_CASE("scaled follows the display scale the layer set", "[ui][widgets]") {
  {
    HeadlessImGui imgui;
    CHECK(ui::scaled(120.0f) == Approx(120.0f));
  }
  {
    HeadlessImGui imgui{2.0f};
    CHECK(ui::scaled(120.0f) == Approx(240.0f));
  }
}

TEST_CASE("the width tokens default to what the editor drew before the theme had them", "[ui][widgets]") {
  const ui::Theme theme;
  CHECK(theme.dialogButtonWidth == 120.0f);
  CHECK(theme.dialogButtonWideWidth == 200.0f);
  CHECK(theme.propertyLabelWidth == 90.0f);
  CHECK(theme.propertyRevertWidth == 50.0f);
  CHECK(theme.overlayInset == 8.0f);
}

TEST_CASE("dialog buttons take their width from the theme and follow the scale", "[ui][widgets]") {
  for (const float scale : {1.0f, 2.0f}) {
    ui::Theme theme;
    theme.dialogButtonWidth = 90.0f;
    theme.dialogButtonWideWidth = 150.0f;
    HeadlessImGui imgui{scale, theme};
    float normal = 0.0f;
    float wide = 0.0f;
    HeadlessImGui::frame([&] {
      (void)ui::dialogButtons({{"OK"}});
      normal = ImGui::GetItemRectSize().x;
      (void)ui::dialogButtons({{"Open without restoring", ui::DialogButtonFlags::Wide}});
      wide = ImGui::GetItemRectSize().x;
    });
    INFO("scale " << scale);
    CHECK(normal == Approx(90.0f * scale));
    CHECK(wide == Approx(150.0f * scale));
  }
}

TEST_CASE("a click presses the dialog button under it and no other", "[ui][widgets]") {
  HeadlessImGui imgui;
  const float width = ui::currentTheme().dialogButtonWidth;
  int last = -1;
  const auto body = [&] {
    const int pressed = ui::dialogButtons({{"Save"}, {"Discard", ui::DialogButtonFlags::Destructive}, {"Cancel"}});
    if (pressed >= 0) {
      last = pressed;
    }
  };
  HeadlessImGui::frame(body);
  for (const int index : {0, 2, 1}) {
    last = -1;
    HeadlessImGui::click(buttonCentre(index, width), body);
    CHECK(last == index);
  }
}

TEST_CASE("Enter presses the default dialog button and Escape the cancel one", "[ui][widgets]") {
  HeadlessImGui imgui;
  int pressed = -1;
  const auto body = [&] {
    const int result = ui::dialogButtons({{"OK", ui::DialogButtonFlags::Default},
                                          {"Discard", ui::DialogButtonFlags::Destructive},
                                          {"Cancel", ui::DialogButtonFlags::Cancel}});
    if (result >= 0) {
      pressed = result;
    }
  };
  HeadlessImGui::frame(body);
  HeadlessImGui::press(ImGuiKey_Enter, body);
  CHECK(pressed == 0);
  pressed = -1;
  HeadlessImGui::press(ImGuiKey_Escape, body);
  CHECK(pressed == 2);
  pressed = -1;
  HeadlessImGui::press(ImGuiKey_Space, body);
  CHECK(pressed == -1);
}

TEST_CASE("one dialog button can be both the default and the cancel", "[ui][widgets]") {
  HeadlessImGui imgui;
  std::string keys;
  const auto body = [&] {
    if (ui::dialogButtons({{"Close", ui::DialogButtonFlags::Default | ui::DialogButtonFlags::Cancel}}) == 0) {
      keys += 'x';
    }
  };
  HeadlessImGui::frame(body);
  HeadlessImGui::press(ImGuiKey_Enter, body);
  HeadlessImGui::press(ImGuiKey_Escape, body);
  CHECK(keys == "xx");
}

TEST_CASE("a disabled dialog button is pressed by neither a click nor its key", "[ui][widgets]") {
  HeadlessImGui imgui;
  bool pressed = false;
  const auto body = [&] {
    pressed = pressed || ui::dialogButtons({{"Save", ui::DialogButtonFlags::Default, false}}) >= 0;
  };
  HeadlessImGui::frame(body);
  HeadlessImGui::press(ImGuiKey_Enter, body);
  HeadlessImGui::click(buttonCentre(0, ui::currentTheme().dialogButtonWidth), body);
  CHECK(!pressed);
}

TEST_CASE("a destructive dialog button is drawn in the theme's stop colour", "[ui][widgets]") {
  const ui::Theme theme = loudTheme();
  HeadlessImGui imgui{1.0f, theme};
  HeadlessImGui::frame([] { (void)ui::dialogButtons({{"Save"}}); });
  CHECK(!drewColour(ui::toU32(theme.stop)));
  HeadlessImGui::frame([] { (void)ui::dialogButtons({{"Discard", ui::DialogButtonFlags::Destructive}}); });
  CHECK(drewColour(ui::toU32(theme.stop)));
}

TEST_CASE("dialog buttons keep each item's ID equal to its label in the caller's stack", "[ui][widgets]") {
  HeadlessImGui imgui;
  ImGuiID expected = 0;
  ImGuiID actual = 0;
  HeadlessImGui::frame([&] {
    ImGui::PushID("modal");
    (void)ui::dialogButtons({{"Cancel", ui::DialogButtonFlags::Cancel}});
    actual = ImGui::GetItemID();
    expected = ImGui::GetID("Cancel");
    ImGui::PopID();
  });
  CHECK(actual == expected);
}

TEST_CASE("a toggle button is drawn pressed while it is selected and reports clicks", "[ui][widgets]") {
  HeadlessImGui imgui;
  const ImVec4 active = ImGui::ColorConvertU32ToFloat4(IM_COL32(200, 20, 20, 255));
  ImGui::GetStyle().Colors[ImGuiCol_ButtonActive] = active;
  HeadlessImGui::frame([] { (void)ui::toggleButton("List", false); });
  CHECK(!drewColour(ui::toU32(active)));
  int clicks = 0;
  const auto body = [&] {
    if (ui::toggleButton("List", true)) {
      ++clicks;
    }
  };
  HeadlessImGui::frame(body);
  CHECK(drewColour(ui::toU32(active)));
  HeadlessImGui::click(buttonCentre(0, 20.0f), body);
  CHECK(clicks == 1);
}

TEST_CASE("state buttons take the colour of their state from the theme", "[ui][widgets]") {
  const ui::Theme theme = loudTheme();
  HeadlessImGui imgui{1.0f, theme};
  HeadlessImGui::frame([] { (void)ui::stateButton("  Play  ", ui::ButtonState::Normal); });
  CHECK(!drewColour(ui::toU32(theme.stop)));
  CHECK(!drewColour(ui::toU32(theme.paused)));
  HeadlessImGui::frame([] { (void)ui::stateButton("  Stop  ", ui::ButtonState::Stop); });
  CHECK(drewColour(ui::toU32(theme.stop)));
  HeadlessImGui::frame([] { (void)ui::stateButton("  Resume  ", ui::ButtonState::Paused); });
  CHECK(drewColour(ui::toU32(theme.paused)));
  // A state button is a small one: no vertical frame padding, so it is shorter than a button.
  float small = 0.0f;
  float normal = 0.0f;
  HeadlessImGui::frame([&] {
    (void)ui::stateButton("Pause", ui::ButtonState::Normal);
    small = ImGui::GetItemRectSize().y;
    ImGui::Button("Button");
    normal = ImGui::GetItemRectSize().y;
  });
  CHECK(small < normal);
}

TEST_CASE("status text is drawn in the colour of its status", "[ui][widgets]") {
  const ui::Theme theme = loudTheme();
  HeadlessImGui imgui{1.0f, theme};
  HeadlessImGui::frame([] { ui::statusText(ui::Status::Error, "failed"); });
  CHECK(drewColour(ui::toU32(theme.danger)));
  CHECK(!drewColour(ui::toU32(theme.warning)));
  HeadlessImGui::frame([] { ui::statusText(ui::Status::Warning, "careful"); });
  CHECK(drewColour(ui::toU32(theme.warning)));
  CHECK(!drewColour(ui::toU32(theme.danger)));
}

TEST_CASE("status text wraps unless asked not to and is never a format string", "[ui][widgets]") {
  HeadlessImGui imgui;
  const std::string sentence = "a message that has to wrap at the width of the window because it is much longer than "
                               "one line of it can hold, however wide that window is made, and then some more words "
                               "and some more words and some more words and some more words and a few more words, "
                               "and a few more words after those, until it is wider than the window by far";
  float wrapped = 0.0f;
  float single = 0.0f;
  HeadlessImGui::frame([&] {
    ui::statusText(ui::Status::Error, sentence);
    wrapped = ImGui::GetItemRectSize().y;
    ui::statusText(ui::Status::Error, sentence, false);
    single = ImGui::GetItemRectSize().y;
  });
  CHECK(wrapped > single);
  CHECK(single == Approx(ImGui::GetFontSize()));

  float width = 0.0f;
  HeadlessImGui::frame([&] {
    ui::statusText(ui::Status::Error, "100%s%d", false);
    width = ImGui::GetItemRectSize().x;
  });
  CHECK(width == Approx(ImGui::CalcTextSize("100%s%d").x));
}

TEST_CASE("a property table has the theme's label width, a stretching value and an optional revert column",
          "[ui][widgets]") {
  for (const float scale : {1.0f, 2.0f}) {
    ui::Theme theme;
    theme.propertyLabelWidth = 90.0f;
    theme.propertyRevertWidth = 50.0f;
    HeadlessImGui imgui{scale, theme};
    float label = 0.0f;
    float value = 0.0f;
    float revert = 0.0f;
    float labelOnly = 0.0f;
    float valueOnly = 0.0f;
    int columns = 0;
    int columnsOnly = 0;
    HeadlessImGui::frame([&] {
      if (ui::beginPropertyTable("property", true)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        const ImGuiTable *table = ImGui::GetCurrentTable();
        label = table->Columns[0].WidthGiven;
        value = table->Columns[1].WidthGiven;
        revert = table->Columns[2].WidthGiven;
        columns = table->ColumnsCount;
        ui::endPropertyTable();
      }
      if (ui::beginPropertyTable("member")) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        const ImGuiTable *table = ImGui::GetCurrentTable();
        labelOnly = table->Columns[0].WidthGiven;
        valueOnly = table->Columns[1].WidthGiven;
        columnsOnly = table->ColumnsCount;
        ui::endPropertyTable();
      }
    });
    INFO("scale " << scale);
    CHECK(columns == 3);
    CHECK(columnsOnly == 2);
    CHECK(label == Approx(90.0f * scale));
    CHECK(revert == Approx(50.0f * scale));
    CHECK(labelOnly == Approx(90.0f * scale));
    // The value takes what is left, and with no revert column that is more. Measured on the very first frame of the
    // table: a stretch column weighed by its content's auto width came out as 0/0 there, and the values drew a few
    // pixels wide on the frame after every selection.
    CHECK(value > 100.0f * scale);
    CHECK(valueOnly > value);
  }
}

TEST_CASE("a property table adds no ID of its own", "[ui][widgets]") {
  HeadlessImGui imgui;
  ImGuiID widget = 0;
  ImGuiID plain = 0;
  HeadlessImGui::frame([&] {
    ImGui::PushID("component");
    if (ui::beginPropertyTable("property", true)) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      (void)ImGui::Button("##value");
      widget = ImGui::GetItemID();
      ui::endPropertyTable();
    }
    ImGui::PopID();
  });
  HeadlessImGui::frame([&] {
    ImGui::PushID("component");
    if (ImGui::BeginTable("property", 3, ImGuiTableFlags_SizingStretchProp)) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      (void)ImGui::Button("##value");
      plain = ImGui::GetItemID();
      ImGui::EndTable();
    }
    ImGui::PopID();
  });
  CHECK(widget != 0);
  CHECK(widget == plain);
}

TEST_CASE("an overlay is a child over the theme's backdrop that takes no input", "[ui][widgets]") {
  const ui::Theme theme = loudTheme();
  HeadlessImGui imgui{1.0f, theme};
  bool open = false;
  float childBgAfter = -1.0f;
  const auto body = [&] {
    open = ui::beginOverlay("overlay");
    if (open) {
      ImGui::TextUnformatted("12.34 ms");
    }
    ui::endOverlay();
    // The colour pushed for the backdrop is popped again.
    childBgAfter = ImGui::GetStyleColorVec4(ImGuiCol_ChildBg).w;
  };
  HeadlessImGui::frame(body);
  HeadlessImGui::frame(body);
  CHECK(open);
  CHECK(childBgAfter == Approx(0.0f));
  CHECK(drewColour(ui::toU32(theme.overlay)));
  const ImGuiWindow *child = nullptr;
  for (const ImGuiWindow *window : ImGui::GetCurrentContext()->Windows) {
    if (window->ParentWindow != nullptr) {
      child = window;
    }
  }
  REQUIRE(child != nullptr);
  CHECK((child->Flags & ImGuiWindowFlags_NoInputs) != 0);
}

TEST_CASE("the overlay inset is the theme's at the style's scale", "[ui][widgets]") {
  {
    HeadlessImGui imgui;
    CHECK(ui::overlayInset().x == Approx(8.0f));
    CHECK(ui::overlayInset().y == Approx(8.0f));
  }
  {
    HeadlessImGui imgui{1.5f};
    CHECK(ui::overlayInset().x == Approx(12.0f));
  }
}

TEST_CASE("an overlay label sits in the bottom-left corner of its area on the label backdrop", "[ui][widgets]") {
  const ui::Theme theme = loudTheme();
  HeadlessImGui imgui{1.0f, theme};
  const ImVec2 areaMin{100.0f, 50.0f};
  const ImVec2 areaSize{400.0f, 300.0f};
  HeadlessImGui::frame([&] { ui::overlayLabel(*ImGui::GetWindowDrawList(), areaMin, areaSize, "Perspective"); });
  CHECK(drewColour(ui::toU32(theme.label)));
  CHECK(drewColour(ui::toU32(theme.labelText)));

  // The backdrop's vertices span from a margin of half the font's size above the area's bottom edge and right of its
  // left one.
  const float margin = ImGui::GetFontSize() * 0.5f;
  const ImU32 backdrop = ui::toU32(theme.label);
  float left = 1e9f;
  float bottom = -1e9f;
  for (const ImDrawList *list : ImGui::GetDrawData()->CmdLists) {
    for (const ImDrawVert &vertex : list->VtxBuffer) {
      if (vertex.col == backdrop) {
        left = std::min(left, vertex.pos.x);
        bottom = std::max(bottom, vertex.pos.y);
      }
    }
  }
  CHECK(left == Approx(areaMin.x + margin).margin(1.0));
  CHECK(bottom == Approx(areaMin.y + areaSize.y - margin).margin(1.0));
}
