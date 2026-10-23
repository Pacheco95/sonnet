#include <sonnet/editor/Layout.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <format>
#include <memory>
#include <system_error>
#include <unordered_map>
#include <vector>

#include <sonnet/core/File.h>
#include <sonnet/core/Log.h>

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

constexpr std::array<Flag, 11> Flags{{
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
    {"CameraGizmos", &ViewState::cameraGizmos},
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
  const char *begin = std::to_address(text.begin());
  const char *end = std::to_address(text.end());
  auto [afterWidth, widthError] = std::from_chars(begin, end, width);
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

// What a layout is checked against, from the limits of Dear ImGui's own ini reader (imgui.cpp, the docking
// settings handler): it stores positions and sizes as 16-bit integers, and its tree walks assume a small tree.
constexpr int MaxDockCoordinate = 32767;
constexpr std::size_t MaxDockNodes = 1024;
constexpr int MaxDockDepth = 64;

// The keys of a dock line, in the only order ImGui's reader accepts them.
constexpr std::array<std::string_view, 14> DockKeys{"ID",
                                                    "Parent",
                                                    "Window",
                                                    "Pos",
                                                    "Size",
                                                    "SizeRef",
                                                    "Split",
                                                    "NoResize",
                                                    "CentralNode",
                                                    "NoTabBar",
                                                    "HiddenTabBar",
                                                    "NoWindowMenuButton",
                                                    "NoCloseButton",
                                                    "Selected"};

// The whole of `text` as a number; nullopt for anything else, a sign or a range beyond a 16-bit value included.
std::optional<int> parseCoordinate(std::string_view text) noexcept {
  int value = 0;
  const char *begin = std::to_address(text.begin());
  const char *end = std::to_address(text.end());
  const auto [after, error] = std::from_chars(begin, end, value);
  if (error != std::errc{} || after != end || value < -MaxDockCoordinate || value > MaxDockCoordinate) {
    return std::nullopt;
  }
  return value;
}

// The whole of `text` as "W,H".
std::optional<std::pair<int, int>> parseCoordinatePair(std::string_view text) noexcept {
  const std::size_t comma = text.find(',');
  if (comma == std::string_view::npos) {
    return std::nullopt;
  }
  const auto first = parseCoordinate(text.substr(0, comma));
  const auto second = parseCoordinate(text.substr(comma + 1));
  if (!first || !second) {
    return std::nullopt;
  }
  return std::pair{*first, *second};
}

// The whole of `text` as "0x" and one to eight hexadecimal digits.
std::optional<std::uint32_t> parseId(std::string_view text) noexcept {
  if (!text.starts_with("0x") || text.size() < 3 || text.size() > 10) {
    return std::nullopt;
  }
  std::uint32_t value = 0;
  const char *begin = std::to_address(text.begin()) + 2;
  const char *end = std::to_address(text.end());
  const auto [after, error] = std::from_chars(begin, end, value, 16);
  if (error != std::errc{} || after != end) {
    return std::nullopt;
  }
  return value;
}

std::vector<std::string_view> splitTokens(std::string_view line) {
  std::vector<std::string_view> tokens;
  for (std::size_t position = 0; position < line.size();) {
    const std::size_t start = line.find_first_not_of(" \t", position);
    if (start == std::string_view::npos) {
      break;
    }
    const std::size_t end = line.find_first_of(" \t", start);
    tokens.push_back(line.substr(start, end == std::string_view::npos ? end : end - start));
    position = end == std::string_view::npos ? line.size() : end;
  }
  return tokens;
}

// One line of the dock tree.
struct DockLine {
  std::uint32_t id{0};
  std::uint32_t parent{0}; // 0 for a root
  bool split{false};
  std::optional<std::pair<int, int>> size;    // a root's
  std::optional<std::pair<int, int>> sizeRef; // a child's
};

// "DockSpace ID=0x1 Window=0x2 Pos=0,19 Size=800,581 Split=X" or "DockNode ID=0x3 Parent=0x1 SizeRef=144,581": the
// grammar Dear ImGui reads (it skips a line out of order silently), with every value in range. Empty when it fits.
std::string parseDockLine(std::string_view line, DockLine &out) {
  const std::vector<std::string_view> tokens = splitTokens(line);
  if (tokens.empty() || (tokens.front() != "DockSpace" && tokens.front() != "DockNode")) {
    return "a dock line starts with neither DockSpace nor DockNode";
  }
  bool hasPos = false;
  std::size_t lastKey = 0;
  for (std::size_t n = 1; n < tokens.size(); ++n) {
    const std::size_t equals = tokens[n].find('=');
    if (equals == std::string_view::npos) {
      return std::format("\"{}\" is not a key=value pair", tokens[n]);
    }
    const std::string_view key = tokens[n].substr(0, equals);
    const std::string_view value = tokens[n].substr(equals + 1);
    const auto found = std::ranges::find(DockKeys, key);
    if (found == DockKeys.end()) {
      return std::format("unknown key \"{}\"", key);
    }
    const auto index = static_cast<std::size_t>(found - DockKeys.begin());
    if ((n > 1 && index <= lastKey) || (n == 1 && index != 0)) {
      return std::format("\"{}\" is out of order", key);
    }
    lastKey = index;
    if (key == "ID" || key == "Parent" || key == "Window" || key == "Selected") {
      const auto id = parseId(value);
      if (!id || (key != "Selected" && *id == 0)) {
        return std::format("{}={} is not an id", key, value);
      }
      if (key == "ID") {
        out.id = *id;
      } else if (key == "Parent") {
        out.parent = *id;
      }
    } else if (key == "Pos" || key == "Size" || key == "SizeRef") {
      const auto pair = parseCoordinatePair(value);
      if (!pair) {
        return std::format("{}={} is not two numbers", key, value);
      }
      hasPos = hasPos || key == "Pos";
      if (key == "Size") {
        out.size = pair;
      } else if (key == "SizeRef") {
        out.sizeRef = pair;
      }
    } else if (key == "Split") {
      if (value != "X" && value != "Y") {
        return std::format("Split={} is neither X nor Y", value);
      }
      out.split = true;
    } else if (!(value == "0" || value == "1")) {
      return std::format("{}={} is not 0 or 1", key, value);
    }
  }
  if (out.id == 0) {
    return "a dock line has no ID";
  }
  if (out.parent == 0 ? !(hasPos && out.size && !out.sizeRef) : (hasPos || out.size || !out.sizeRef)) {
    return out.parent == 0 ? "a root needs Pos and Size, and no SizeRef" : "a child needs SizeRef, and no Pos or Size";
  }
  return {};
}

// The tree of dock lines, checked as they come: ImGui writes a parent before its children, and builds each node
// from the nodes it has seen so far.
class DockTree {
public:
  // The reason `line` cannot be added, empty when it was.
  std::string add(const DockLine &line) {
    if (m_nodes.size() >= MaxDockNodes) {
      return "too many dock nodes";
    }
    if (m_nodes.contains(line.id)) {
      return std::format("dock node 0x{:08X} appears twice", line.id);
    }
    int depth = 0;
    if (line.parent == 0) {
      if (line.size->first <= 0 || line.size->second <= 0) {
        return std::format("dock root 0x{:08X} has no size", line.id);
      }
    } else {
      const auto parent = m_nodes.find(line.parent);
      if (parent == m_nodes.end()) {
        return std::format("dock node 0x{:08X} comes before its parent 0x{:08X}, or the parent is missing", line.id,
                           line.parent);
      }
      if (!parent->second.split || parent->second.children >= 2) {
        return std::format("dock node 0x{:08X} is not a split with room for a child", line.parent);
      }
      if (line.sizeRef->first <= 0 || line.sizeRef->second <= 0) {
        return std::format("dock node 0x{:08X} has no size", line.id);
      }
      ++parent->second.children;
      depth = parent->second.depth + 1;
      if (depth > MaxDockDepth) {
        return "the dock tree is too deep";
      }
    }
    m_nodes.emplace(line.id, Node{.split = line.split, .depth = depth});
    return {};
  }

  [[nodiscard]] bool contains(std::uint32_t id) const noexcept {
    return m_nodes.contains(id);
  }

private:
  struct Node {
    bool split{false};
    int children{0};
    int depth{0};
  };
  std::unordered_map<std::uint32_t, Node> m_nodes;
};

// A line of the view section: "Key=0" or "Key=1" for a flag, "DockSize=W,H" for the dock root's size. A key this
// editor does not know is kept out of the check, as the reader skips it.
std::string checkViewLine(std::string_view line) {
  const std::size_t equals = line.find('=');
  if (equals == std::string_view::npos) {
    return std::format("\"{}\" is not a key=value pair", line);
  }
  const std::string_view key = line.substr(0, equals);
  const std::string_view value = line.substr(equals + 1);
  if (key == "DockSize") {
    return parseCoordinatePair(value) ? std::string{} : std::format("DockSize={} is not two numbers", value);
  }
  const bool known = std::ranges::any_of(Flags, [&](const Flag &flag) { return key == flag.key; });
  return !known || value == "0" || value == "1" ? std::string{} : std::format("{}={} is not 0 or 1", key, value);
}

// The ids a window line names in `DockId=0x...,order`; empty when it names none.
std::optional<std::uint32_t> windowDockId(std::string_view line) {
  constexpr std::string_view key = "DockId=";
  if (!line.starts_with(key)) {
    return std::nullopt;
  }
  const std::string_view value = line.substr(key.size());
  return parseId(value.substr(0, value.find(',')));
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

std::optional<std::string> layoutIniProblem(std::string_view ini) {
  if (ini.empty()) {
    return std::nullopt;
  }
  if (ini.back() != '\n') {
    return "the file ends in the middle of a line";
  }
  enum class Section : std::uint8_t {
    Other,
    Window,
    Dock,
    View
  };
  Section section = Section::Other;
  DockTree tree;
  std::vector<std::uint32_t> windowDocks;
  int number = 0;
  for (std::size_t position = 0; position < ini.size(); ++number) {
    const std::size_t newline = ini.find('\n', position);
    std::string_view line = ini.substr(position, newline - position);
    position = newline + 1;
    if (line.ends_with('\r')) {
      line.remove_suffix(1);
    }
    if (line.empty()) {
      continue;
    }
    const auto fail = [&](std::string_view reason) { return std::format("line {}: {}", number + 1, reason); };
    if (line.front() == '[') {
      section = line == DockHeader             ? Section::Dock
                : line == SectionHeader        ? Section::View
                : line.starts_with("[Window]") ? Section::Window
                                               : Section::Other;
      continue;
    }
    if (section == Section::Dock) {
      if (const std::size_t comment = line.find(" ;"); comment != std::string_view::npos) {
        line = line.substr(0, comment);
      }
      DockLine dock;
      std::string problem = parseDockLine(line, dock);
      if (problem.empty()) {
        problem = tree.add(dock);
      }
      if (!problem.empty()) {
        return fail(problem);
      }
    } else if (section == Section::View) {
      if (const std::string problem = checkViewLine(line); !problem.empty()) {
        return fail(problem);
      }
    } else if (section == Section::Window) {
      if (const auto id = windowDockId(line)) {
        windowDocks.push_back(*id);
      }
    }
  }
  for (const std::uint32_t id : windowDocks) {
    if (!tree.contains(id)) {
      return std::format("a window is docked in 0x{:08X}, which the file does not describe", id);
    }
  }
  return std::nullopt;
}

bool setAsideBadLayout(const std::filesystem::path &file) {
  const auto bytes = core::readFile(file);
  if (!bytes) {
    return true;
  }
  const std::string_view text{reinterpret_cast<const char *>(bytes->data()), bytes->size()};
  const auto problem = layoutIniProblem(text);
  if (!problem) {
    return true;
  }
  std::filesystem::path kept = file;
  kept += ".rejected";
  std::error_code error;
  std::filesystem::rename(file, kept, error);
  if (error) {
    SONNET_LOG_WARN("{} is not a usable layout ({}) and could not be renamed ({}); the layout is not kept this session",
                    file.string(), *problem, error.message());
    return false;
  }
  SONNET_LOG_WARN("{} is not a usable layout ({}); the default layout is used and the file is kept as {}",
                  file.filename().string(), *problem, kept.filename().string());
  return true;
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
