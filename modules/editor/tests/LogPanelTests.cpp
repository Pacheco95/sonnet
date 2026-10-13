#include <sonnet/editor/LogPanel.h>

#include <sonnet/core/Log.h>

#include <catch2/catch_test_macros.hpp>

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <format>

using namespace sonnet::editor;

TEST_CASE("the log buffer receives entries from the logging macros with their origin", "[editor][log]") {
  LogPanel panel;
  const std::uint64_t before = panel.buffer().revision();
  SONNET_LOG_WARN("hello {}", 42);
  const int line = __LINE__ - 1;
  REQUIRE(panel.buffer().revision() == before + 1);
  const std::vector<LogEntry> entries = panel.buffer().snapshot();
  REQUIRE(entries.size() == 1);
  const LogEntry &entry = entries.back();
  REQUIRE(entry.level == spdlog::level::warn);
  REQUIRE(entry.module == "editor_tests");
  REQUIRE(entry.file == "LogPanelTests.cpp");
  REQUIRE(entry.line == line);
  REQUIRE(entry.message == "hello 42");
  REQUIRE(entry.time.size() == 12); // HH:MM:SS.mmm
}

TEST_CASE("the log buffer keeps the newest entries and can be cleared", "[editor][log]") {
  LogPanel panel;
  for (std::size_t i = 0; i < LogBuffer::Capacity + 10; ++i) {
    SONNET_LOG_INFO("entry {}", i);
  }
  const std::vector<LogEntry> entries = panel.buffer().snapshot();
  REQUIRE(entries.size() == LogBuffer::Capacity);
  REQUIRE(entries.front().message == "entry 10");
  REQUIRE(entries.back().message == std::format("entry {}", LogBuffer::Capacity + 9));
  auto &buffer = const_cast<LogBuffer &>(panel.buffer());
  buffer.clear();
  REQUIRE(panel.buffer().snapshot().empty());
}

TEST_CASE("a destroyed panel stops receiving entries", "[editor][log]") {
  std::shared_ptr<const LogBuffer> buffer;
  {
    LogPanel panel;
    SONNET_LOG_INFO("while alive");
    REQUIRE(panel.buffer().snapshot().size() == 1);
  }
  // Logging after the panel is gone must not touch the removed sink.
  SONNET_LOG_INFO("after");
}

TEST_CASE("a log entry formats as time, level, module, location and message", "[editor][log]") {
  LogEntry entry{.level = spdlog::level::err,
                 .time = "12:34:56.789",
                 .module = "renderer",
                 .file = "Renderer.cpp",
                 .path = "modules/renderer/src/Renderer.cpp",
                 .line = 42,
                 .message = "device lost"};
  REQUIRE(formatLogEntry(entry) == "12:34:56.789 [error] [renderer] Renderer.cpp:42 device lost");
  entry.file.clear();
  REQUIRE(formatLogEntry(entry) == "12:34:56.789 [error] [renderer] device lost");
}

namespace {

// A Dear ImGui context without a renderer: enough to lay out the panel and feed it input.
struct ImGuiContextGuard {
  ImGuiContextGuard() {
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2{800.0f, 600.0f};
    io.IniFilename = nullptr;
    unsigned char *pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
  }
  ~ImGuiContextGuard() {
    ImGui::DestroyContext();
  }
  ImGuiContextGuard(const ImGuiContextGuard &) = delete;
  ImGuiContextGuard &operator=(const ImGuiContextGuard &) = delete;
};

struct PanelDriver {
  LogPanel &panel;

  void frame() const {
    ImGui::NewFrame();
    // Undocked, the window would fit its first, empty frame.
    ImGui::SetNextWindowPos(ImVec2{0.0f, 0.0f});
    ImGui::SetNextWindowSize(ImVec2{800.0f, 600.0f});
    bool open = true;
    panel.draw(open);
    ImGui::Render();
  }
  // Moves the mouse down the entries, `x` from their left edge (from the right when negative),
  // until the item `label` of the row of `sequence` is hovered.
  [[nodiscard]] bool hover(std::uint64_t sequence, const char *label, float x) const {
    ImGuiWindow *log = ImGui::FindWindowByName("Log");
    REQUIRE(log != nullptr);
    ImGuiWindow *entries = nullptr;
    for (ImGuiWindow *window : ImGui::GetCurrentContext()->Windows) {
      if (window->ParentWindow == log) {
        entries = window;
      }
    }
    REQUIRE(entries != nullptr);
    const int key = static_cast<int>(sequence); // the panel's PushID
    const ImGuiID id = ImHashStr(label, 0, ImHashData(&key, sizeof(key), entries->ID));
    for (float y = entries->Pos.y; y < entries->Pos.y + entries->Size.y; y += 2.0f) {
      ImGui::GetIO().AddMousePosEvent(x >= 0.0f ? entries->Pos.x + x : entries->Pos.x + entries->Size.x + x, y);
      frame();
      if (ImGui::GetCurrentContext()->HoveredIdPreviousFrame == id) {
        return true;
      }
    }
    return false;
  }
  void click(ImGuiKeyChord modifiers = ImGuiKey_None, ImGuiMouseButton button = ImGuiMouseButton_Left) const {
    ImGuiIO &io = ImGui::GetIO();
    setModifiers(modifiers, true);
    io.AddMouseButtonEvent(button, true);
    frame();
    io.AddMouseButtonEvent(button, false);
    frame();
    setModifiers(modifiers, false);
    frame();
  }
  void press(ImGuiKeyChord chord) const {
    ImGuiIO &io = ImGui::GetIO();
    const auto key = static_cast<ImGuiKey>(chord & ~ImGuiMod_Mask_);
    setModifiers(chord, true);
    io.AddKeyEvent(key, true);
    frame();
    io.AddKeyEvent(key, false);
    setModifiers(chord, false);
    frame();
  }
  static void setModifiers(ImGuiKeyChord chord, bool down) {
    ImGuiIO &io = ImGui::GetIO();
    if ((chord & ImGuiMod_Ctrl) != 0) {
      // A backend sends Cmd as Super, which ImGui on macOS swaps to ImGuiMod_Ctrl; Ctrl there
      // becomes Super, and Ctrl+click a right-click.
      io.AddKeyEvent(io.ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl, down);
    }
    if ((chord & ImGuiMod_Shift) != 0) {
      io.AddKeyEvent(ImGuiMod_Shift, down);
    }
  }
};

} // namespace

TEST_CASE("log rows are selected with the mouse and copied with ctrl+c", "[editor][log]") {
  ImGuiContextGuard imgui;
  LogPanel panel;
  std::vector<std::pair<std::string, int>> opened;
  panel.setLocationHandler([&](const std::string &path, int line) { opened.emplace_back(path, line); });
  SONNET_LOG_INFO("first");
  SONNET_LOG_WARN("second");
  SONNET_LOG_ERROR("third");
  const std::vector<LogEntry> entries = panel.buffer().snapshot();
  REQUIRE(entries.size() == 3);
  const PanelDriver driver{panel};
  driver.frame();
  REQUIRE(panel.selectionText().empty());

  // Clicking the row's message, right of the location, selects it without opening the file.
  REQUIRE(driver.hover(entries[0].sequence, "##row", -20.0f));
  driver.click();
  REQUIRE(panel.selectionText() == formatLogEntry(entries[0]) + "\n");
  REQUIRE(driver.hover(entries[2].sequence, "##row", -20.0f));
  driver.click(ImGuiMod_Shift);
  REQUIRE(panel.selectionText() ==
          formatLogEntry(entries[0]) + "\n" + formatLogEntry(entries[1]) + "\n" + formatLogEntry(entries[2]) + "\n");
  REQUIRE(driver.hover(entries[1].sequence, "##row", -20.0f));
  driver.click(ImGuiMod_Ctrl);
  const std::string expected = formatLogEntry(entries[0]) + "\n" + formatLogEntry(entries[2]) + "\n";
  REQUIRE(panel.selectionText() == expected);
  REQUIRE(opened.empty());

  driver.press(ImGuiMod_Ctrl | ImGuiKey_C);
  REQUIRE(std::string{ImGui::GetClipboardText()} == expected);
  REQUIRE(expected.find(std::format("LogPanelTests.cpp:{}", entries[0].line)) != std::string::npos);

  // The location is still a link: it opens the file and leaves the selection alone.
  const std::string location = std::format("{}:{}", entries[1].file, entries[1].line);
  bool found = false;
  for (float x = 0.0f; x < 400.0f && !found; x += 20.0f) {
    found = driver.hover(entries[1].sequence, location.c_str(), x);
  }
  REQUIRE(found);
  driver.click();
  REQUIRE(opened.size() == 1);
  REQUIRE(opened.front().first == entries[1].path);
  REQUIRE(opened.front().second == entries[1].line);
  REQUIRE(panel.selectionText() == expected);

  // A right-click on an unselected row selects it, and the menu's Copy copies it.
  REQUIRE(driver.hover(entries[1].sequence, "##row", -20.0f));
  driver.click(ImGuiKey_None, ImGuiMouseButton_Right);
  REQUIRE(panel.selectionText() == formatLogEntry(entries[1]) + "\n");
  REQUIRE(!ImGui::GetCurrentContext()->OpenPopupStack.empty());
  ImGuiWindow *menu = ImGui::GetCurrentContext()->OpenPopupStack.back().Window;
  REQUIRE(menu != nullptr);
  const ImGuiID copy = ImHashStr("Copy", 0, menu->ID);
  bool overCopy = false;
  for (float y = menu->Pos.y; y < menu->Pos.y + menu->Size.y && !overCopy; y += 2.0f) {
    ImGui::GetIO().AddMousePosEvent(menu->Pos.x + 20.0f, y);
    driver.frame();
    overCopy = ImGui::GetCurrentContext()->HoveredIdPreviousFrame == copy;
  }
  REQUIRE(overCopy);
  driver.click();
  REQUIRE(std::string{ImGui::GetClipboardText()} == formatLogEntry(entries[1]) + "\n");

  panel.copyAll();
  REQUIRE(std::string{ImGui::GetClipboardText()} ==
          formatLogEntry(entries[0]) + "\n" + formatLogEntry(entries[1]) + "\n" + formatLogEntry(entries[2]) + "\n");

  // A cleared buffer leaves nothing selected.
  const_cast<LogBuffer &>(panel.buffer()).clear();
  driver.frame();
  REQUIRE(panel.selectionText().empty());
}
