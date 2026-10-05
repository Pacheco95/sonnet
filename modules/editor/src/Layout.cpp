#include <sonnet/editor/Layout.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <format>
#include <system_error>

#include <imgui.h>
#include <imgui_internal.h>

namespace sonnet::editor {

namespace {

constexpr const char *SectionType = "SonnetView";
constexpr std::string_view SectionHeader = "[SonnetView][View]";
constexpr std::string_view DockHeader = "[Docking][Data]";
constexpr std::size_t MaxNameLength = 64;

struct Flag {
  const char *key;
  bool ViewState::*member;
};

constexpr std::array<Flag, 10> Flags{{
    {"Viewport", &ViewState::viewport},
    {"Game", &ViewState::game},
    {"Hierarchy", &ViewState::hierarchy},
    {"Inspector", &ViewState::inspector},
    {"Log", &ViewState::log},
    {"Assets", &ViewState::assets},
    {"Statistics", &ViewState::statistics},
    {"Overlay", &ViewState::overlay},
    {"Colliders", &ViewState::colliders},
    {"LightGizmos", &ViewState::lightGizmos},
}};

bool iequals(std::string_view a, std::string_view b) noexcept {
  return std::ranges::equal(a, b, [](char x, char y) {
    return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
  });
}

// Names Windows will not open as files, whatever the extension.
bool isDeviceName(std::string_view name) noexcept {
  for (const std::string_view device : {"CON", "PRN", "AUX", "NUL"}) {
    if (iequals(name, device)) {
      return true;
    }
  }
  return name.size() == 4 && (iequals(name.substr(0, 3), "COM") || iequals(name.substr(0, 3), "LPT")) &&
         name[3] >= '1' && name[3] <= '9';
}

// "W,H" at the start of `text`.
std::optional<std::pair<int, int>> parseSize(std::string_view text) noexcept {
  int width = 0;
  int height = 0;
  const char *end = text.data() + text.size();
  auto [afterWidth, widthError] = std::from_chars(text.data(), end, width);
  if (widthError != std::errc{} || afterWidth == end || *afterWidth != ',') {
    return std::nullopt;
  }
  auto [afterHeight, heightError] = std::from_chars(afterWidth + 1, end, height);
  if (heightError != std::errc{}) {
    return std::nullopt;
  }
  return std::pair{width, height};
}

std::optional<std::pair<int, int>> recordedDockSize(std::string_view ini) noexcept {
  const std::size_t section = ini.find(SectionHeader);
  if (section == std::string_view::npos) {
    return std::nullopt;
  }
  constexpr std::string_view key = "\nDockSize=";
  const std::size_t line = ini.find(key, section);
  const std::size_t next = ini.find("\n[", section);
  if (line == std::string_view::npos || line > next) {
    return std::nullopt;
  }
  return parseSize(ini.substr(line + key.size()));
}

// One dock line, "DockNode ID=0x1 Parent=0x2 SizeRef=360,300 Split=Y": the Size and SizeRef tokens scaled.
std::string scaleSizes(std::string_view line, float scaleX, float scaleY) {
  std::string out;
  std::size_t position = 0;
  while (position <= line.size()) {
    const std::size_t space = line.find(' ', position);
    const std::size_t end = space == std::string_view::npos ? line.size() : space;
    const std::string_view token = line.substr(position, end - position);
    const std::size_t equals = token.find('=');
    const std::string_view key = token.substr(0, equals);
    const auto size = (key == "Size" || key == "SizeRef") ? parseSize(token.substr(equals + 1)) : std::nullopt;
    if (size) {
      out += std::format("{}={},{}", key, std::lround(static_cast<float>(size->first) * scaleX),
                         std::lround(static_cast<float>(size->second) * scaleY));
    } else {
      out.append(token);
    }
    if (space == std::string_view::npos) {
      break;
    }
    out += ' ';
    position = end + 1;
  }
  return out;
}

} // namespace

std::string_view layoutName(BuiltinLayout layout) noexcept {
  return layout == BuiltinLayout::Play ? "Play" : "Default";
}

std::optional<BuiltinLayout> builtinLayout(std::string_view name) noexcept {
  for (const BuiltinLayout layout : {BuiltinLayout::Default, BuiltinLayout::Play}) {
    if (iequals(name, layoutName(layout))) {
      return layout;
    }
  }
  return std::nullopt;
}

ViewState builtinViewState(BuiltinLayout layout) noexcept {
  ViewState view;
  if (layout == BuiltinLayout::Play) {
    view.game = true;
    view.log = false;
    view.assets = false;
    view.statistics = false;
    view.colliders = true;
  }
  return view;
}

std::string sanitizeLayoutName(std::string_view name) {
  std::string out;
  for (const char c : name) {
    const auto u = static_cast<unsigned char>(c);
    out += (std::isalnum(u) != 0 || c == ' ' || c == '-' || c == '_') ? c : '_';
  }
  const std::size_t first = out.find_first_not_of(' ');
  if (first == std::string::npos) {
    return {};
  }
  out = out.substr(first, out.find_last_not_of(' ') - first + 1);
  out = out.substr(0, MaxNameLength);
  if (out.find_first_not_of(' ') == std::string::npos) {
    return {};
  }
  if (std::size_t last = out.find_last_not_of(' '); last + 1 < out.size()) {
    out.resize(last + 1);
  }
  return isDeviceName(out) ? out + "_" : out;
}

std::vector<std::string> listLayouts(const std::filesystem::path &directory) {
  std::vector<std::string> names;
  std::error_code error;
  for (std::filesystem::directory_iterator it{directory, error}, end; !error && it != end; it.increment(error)) {
    if (it->is_regular_file(error) && it->path().extension() == ".ini") {
      names.push_back(it->path().stem().string());
    }
  }
  std::ranges::sort(names);
  return names;
}

bool isLayoutIni(std::string_view ini) noexcept {
  if (ini.find(SectionHeader) == std::string_view::npos) {
    return false;
  }
  const std::size_t dock = ini.find(DockHeader);
  return dock != std::string_view::npos && ini.find("\nDockSpace ", dock) != std::string_view::npos;
}

std::string scaleLayoutIni(std::string_view ini, float width, float height) {
  const auto saved = recordedDockSize(ini);
  if (!saved || saved->first <= 0 || saved->second <= 0 || width <= 0.0f || height <= 0.0f) {
    return std::string{ini};
  }
  const float scaleX = width / static_cast<float>(saved->first);
  const float scaleY = height / static_cast<float>(saved->second);
  std::string out;
  out.reserve(ini.size());
  bool inDock = false;
  for (std::size_t position = 0; position < ini.size();) {
    const std::size_t newline = ini.find('\n', position);
    const std::size_t end = newline == std::string_view::npos ? ini.size() : newline;
    const std::string_view line = ini.substr(position, end - position);
    if (!line.empty() && line.front() == '[') {
      inDock = line == DockHeader;
      out.append(line);
    } else {
      out += inDock ? scaleSizes(line, scaleX, scaleY) : std::string{line};
    }
    if (newline != std::string_view::npos) {
      out += '\n';
    }
    position = end + 1;
  }
  return out;
}

LayoutSettings::LayoutSettings(ViewState &view) : m_view(view) {
  ImGuiSettingsHandler handler;
  handler.TypeName = SectionType;
  handler.TypeHash = ImHashStr(SectionType);
  handler.UserData = this;
  handler.ReadOpenFn = [](ImGuiContext *, ImGuiSettingsHandler *self, const char *name) -> void * {
    return std::string_view{name} == "View" ? self->UserData : nullptr;
  };
  handler.ReadLineFn = [](ImGuiContext *, ImGuiSettingsHandler *, void *entry, const char *line) {
    ViewState &state = static_cast<LayoutSettings *>(entry)->m_view;
    const std::string_view text{line};
    const std::size_t equals = text.find('=');
    if (equals == std::string_view::npos) {
      return;
    }
    for (const Flag &flag : Flags) {
      if (text.substr(0, equals) == flag.key) {
        state.*flag.member = text.substr(equals + 1) != "0";
        return;
      }
    }
  };
  handler.WriteAllFn = [](ImGuiContext *, ImGuiSettingsHandler *self, ImGuiTextBuffer *out) {
    const auto *settings = static_cast<const LayoutSettings *>(self->UserData);
    out->appendf("[%s][View]\n", SectionType);
    for (const Flag &flag : Flags) {
      out->appendf("%s=%d\n", flag.key, settings->m_view.*flag.member ? 1 : 0);
    }
    if (const ImGuiDockNode *root =
            settings->m_dockspace != 0 ? ImGui::DockBuilderGetNode(settings->m_dockspace) : nullptr) {
      out->appendf("DockSize=%d,%d\n", static_cast<int>(root->Size.x), static_cast<int>(root->Size.y));
    }
    out->append("\n");
  };
  ImGui::RemoveSettingsHandler(SectionType);
  ImGui::AddSettingsHandler(&handler);
}

LayoutSettings::~LayoutSettings() {
  ImGui::RemoveSettingsHandler(SectionType);
}

} // namespace sonnet::editor
