#include <sonnet/ui/Widgets.h>

namespace sonnet::ui {

namespace {

bool has(DialogButtonFlags flags, DialogButtonFlags flag) {
  return (static_cast<unsigned>(flags) & static_cast<unsigned>(flag)) != 0;
}

// Pushes `colour` as the button's, when there is one, and pops it at the end of the scope.
class ButtonColour {
public:
  explicit ButtonColour(const ImVec4 *colour) : m_pushed(colour != nullptr) {
    if (m_pushed) {
      ImGui::PushStyleColor(ImGuiCol_Button, *colour);
    }
  }
  ~ButtonColour() {
    if (m_pushed) {
      ImGui::PopStyleColor();
    }
  }
  ButtonColour(const ButtonColour &) = delete;
  ButtonColour &operator=(const ButtonColour &) = delete;

private:
  bool m_pushed;
};

} // namespace

float scaled(float base) {
  // The scale `ScaleAllSizes` applied to the style (applyTheme). Dear ImGui marks the field as not for use yet, so this
  // is the one place that reads it; #390 gives the theme a scale of its own.
  return base * ImGui::GetStyle()._MainScale;
}

int dialogButtons(std::initializer_list<DialogButton> buttons) {
  const Theme &theme = currentTheme();
  int pressed = -1;
  int index = 0;
  for (const DialogButton &button : buttons) {
    if (index > 0) {
      ImGui::SameLine();
    }
    const float width =
        scaled(has(button.flags, DialogButtonFlags::Wide) ? theme.dialogButtonWideWidth : theme.dialogButtonWidth);
    ImGui::BeginDisabled(!button.enabled);
    {
      const ButtonColour colour{has(button.flags, DialogButtonFlags::Destructive) ? &theme.stop : nullptr};
      const bool clicked = ImGui::Button(button.label, ImVec2{width, 0.0f});
      const bool enter = has(button.flags, DialogButtonFlags::Default) && ImGui::IsKeyPressed(ImGuiKey_Enter, false);
      const bool escape = has(button.flags, DialogButtonFlags::Cancel) && ImGui::IsKeyPressed(ImGuiKey_Escape, false);
      // A disabled button is not pressed by its key either. The first one pressed wins.
      if ((clicked || ((enter || escape) && button.enabled)) && pressed < 0) {
        pressed = index;
      }
    }
    ImGui::EndDisabled();
    ++index;
  }
  return pressed;
}

bool toggleButton(const char *label, bool selected) {
  const ButtonColour colour{selected ? &ImGui::GetStyle().Colors[ImGuiCol_ButtonActive] : nullptr};
  return ImGui::Button(label);
}

bool stateButton(const char *label, ButtonState state) {
  const Theme &theme = currentTheme();
  const ImVec4 *colour = state == ButtonState::Stop     ? &theme.stop
                         : state == ButtonState::Paused ? &theme.paused
                                                        : nullptr;
  const ButtonColour pushed{colour};
  return ImGui::SmallButton(label);
}

void statusText(Status status, std::string_view message, bool wrap) {
  const Theme &theme = currentTheme();
  ImGui::PushStyleColor(ImGuiCol_Text, status == Status::Error ? theme.danger : theme.warning);
  if (wrap) {
    ImGui::PushTextWrapPos(0.0f);
  }
  ImGui::TextUnformatted(message.data(), message.data() + message.size());
  if (wrap) {
    ImGui::PopTextWrapPos();
  }
  ImGui::PopStyleColor();
}

bool beginPropertyTable(const char *id, bool revertColumn) {
  if (!ImGui::BeginTable(id, revertColumn ? 3 : 2, ImGuiTableFlags_SizingStretchProp)) {
    return false;
  }
  const Theme &theme = currentTheme();
  ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, scaled(theme.propertyLabelWidth));
  // An explicit weight: without one, SizingStretchProp weighs a stretch column by its content's
  // auto width, which a table's first frame has not measured yet. The weight came out as 0/0, so on
  // the frame after every selection the values drew a few pixels wide, and grew back over the next.
  ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 1.0f);
  if (revertColumn) {
    ImGui::TableSetupColumn("revert", ImGuiTableColumnFlags_WidthFixed, scaled(theme.propertyRevertWidth));
  }
  return true;
}

void endPropertyTable() {
  ImGui::EndTable();
}

ImVec2 overlayInset() {
  const float inset = scaled(currentTheme().overlayInset);
  return ImVec2{inset, inset};
}

bool beginOverlay(const char *id) {
  ImGui::PushStyleColor(ImGuiCol_ChildBg, currentTheme().overlay);
  return ImGui::BeginChild(id, ImVec2{0.0f, 0.0f},
                           ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY |
                               ImGuiChildFlags_AlwaysUseWindowPadding,
                           ImGuiWindowFlags_NoInputs);
}

void endOverlay() {
  ImGui::EndChild();
  ImGui::PopStyleColor();
}

void overlayLabel(ImDrawList &drawList, ImVec2 areaMin, ImVec2 areaSize, const char *text) {
  const Theme &theme = currentTheme();
  const float fontSize = ImGui::GetFontSize();
  const ImVec2 textSize = ImGui::CalcTextSize(text);
  const float margin = fontSize * 0.5f;
  const ImVec2 padding{fontSize * 0.4f, fontSize * 0.2f};
  const ImVec2 min{areaMin.x + margin, areaMin.y + areaSize.y - margin - textSize.y - 2.0f * padding.y};
  const ImVec2 max{min.x + textSize.x + 2.0f * padding.x, areaMin.y + areaSize.y - margin};
  drawList.PushClipRect(areaMin, ImVec2{areaMin.x + areaSize.x, areaMin.y + areaSize.y}, true);
  drawList.AddRectFilled(min, max, toU32(theme.label), fontSize * 0.2f);
  drawList.AddText(ImVec2{min.x + padding.x, min.y + padding.y}, toU32(theme.labelText), text);
  drawList.PopClipRect();
}

} // namespace sonnet::ui
