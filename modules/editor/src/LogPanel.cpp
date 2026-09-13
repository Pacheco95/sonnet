#include <sonnet/editor/LogPanel.h>

#include <sonnet/core/Log.h>

#include <imgui.h>
#include <imgui_stdlib.h>
#include <spdlog/details/log_msg.h>
#include <spdlog/details/os.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>

namespace sonnet::editor {

namespace {

const char *levelName(spdlog::level::level_enum level) {
  switch (level) {
  case spdlog::level::trace:
    return "trace";
  case spdlog::level::debug:
    return "debug";
  case spdlog::level::info:
    return "info";
  case spdlog::level::warn:
    return "warn";
  case spdlog::level::err:
    return "error";
  case spdlog::level::critical:
    return "critical";
  default:
    return "off";
  }
}

ImVec4 levelColor(spdlog::level::level_enum level) {
  switch (level) {
  case spdlog::level::trace:
    return {0.55f, 0.55f, 0.55f, 1.0f};
  case spdlog::level::debug:
    return {0.6f, 0.75f, 0.95f, 1.0f};
  case spdlog::level::warn:
    return {0.95f, 0.8f, 0.3f, 1.0f};
  case spdlog::level::err:
    return {0.95f, 0.4f, 0.4f, 1.0f};
  case spdlog::level::critical:
    return {1.0f, 0.3f, 0.8f, 1.0f};
  default:
    return {0.9f, 0.9f, 0.9f, 1.0f};
  }
}

bool containsCaseInsensitive(std::string_view text, std::string_view needle) {
  if (needle.empty()) {
    return true;
  }
  const auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
  return std::ranges::search(text, needle, [&](char a, char b) {
           return lower(static_cast<unsigned char>(a)) == lower(static_cast<unsigned char>(b));
         }).begin() != text.end();
}

} // namespace

void LogBuffer::log(const spdlog::details::log_msg &msg) {
  // spdlog's localtime rather than std::chrono's time zones, which Apple's libc++ lacks.
  const std::tm local = spdlog::details::os::localtime(std::chrono::system_clock::to_time_t(msg.time));
  const auto milliseconds =
      std::chrono::duration_cast<std::chrono::milliseconds>(msg.time.time_since_epoch()).count() % 1000;
  LogEntry entry{
      .level = msg.level,
      .time = std::format("{:02}:{:02}:{:02}.{:03}", local.tm_hour, local.tm_min, local.tm_sec, milliseconds),
      .module = std::string{msg.logger_name.data(), msg.logger_name.size()},
      .file = msg.source.filename != nullptr ? std::filesystem::path{msg.source.filename}.filename().string() : "",
      .path = msg.source.filename != nullptr ? msg.source.filename : "",
      .line = msg.source.line,
      .message = std::string{msg.payload.data(), msg.payload.size()},
  };
  std::scoped_lock lock(m_mutex);
  if (m_entries.size() >= Capacity) {
    m_entries.pop_front();
  }
  m_entries.push_back(std::move(entry));
  ++m_revision;
}

std::vector<LogEntry> LogBuffer::snapshot() const {
  std::scoped_lock lock(m_mutex);
  return {m_entries.begin(), m_entries.end()};
}

std::uint64_t LogBuffer::revision() const {
  std::scoped_lock lock(m_mutex);
  return m_revision;
}

void LogBuffer::clear() {
  std::scoped_lock lock(m_mutex);
  m_entries.clear();
  ++m_revision;
}

LogPanel::LogPanel() : m_buffer(std::make_shared<LogBuffer>()) {
  core::Log::addSink(m_buffer);
}

LogPanel::~LogPanel() {
  core::Log::removeSink(m_buffer);
}

void LogPanel::draw(bool &open) {
  if (!ImGui::Begin("Log", &open)) {
    ImGui::End();
    return;
  }
  if (const std::uint64_t revision = m_buffer->revision(); revision != m_revision) {
    m_entries = m_buffer->snapshot();
    m_revision = revision;
  }

  constexpr const char *levels[] = {"trace", "debug", "info", "warn", "error", "critical"};
  ImGui::SetNextItemWidth(90.0f);
  ImGui::Combo("##level", &m_minimumLevel, levels, IM_ARRAYSIZE(levels));
  ImGui::SameLine();
  ImGui::SetNextItemWidth(-160.0f);
  ImGui::InputTextWithHint("##filter", "filter", &m_filter);
  ImGui::SameLine();
  ImGui::Checkbox("scroll", &m_autoScroll);
  ImGui::SameLine();
  if (ImGui::Button("clear")) {
    m_buffer->clear();
  }

  if (ImGui::BeginChild("entries", ImVec2{0.0f, 0.0f}, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar)) {
    int index = 0;
    for (const LogEntry &entry : m_entries) {
      if (static_cast<int>(entry.level) < m_minimumLevel ||
          !(containsCaseInsensitive(entry.message, m_filter) || containsCaseInsensitive(entry.module, m_filter))) {
        continue;
      }
      ImGui::PushID(index++);
      ImGui::PushStyleColor(ImGuiCol_Text, levelColor(entry.level));
      ImGui::TextUnformatted(entry.time.c_str());
      ImGui::SameLine();
      ImGui::Text("[%s] [%s]", levelName(entry.level), entry.module.c_str());
      ImGui::SameLine();
      // The location opens the external editor from the preferences (docs/conventions.md).
      const std::string location = std::format("{}:{}", entry.file, entry.line);
      if (m_openLocation && !entry.path.empty()) {
        ImGui::PopStyleColor();
        if (ImGui::TextLink(location.c_str())) {
          m_openLocation(entry.path, entry.line);
        }
        ImGui::PushStyleColor(ImGuiCol_Text, levelColor(entry.level));
      } else {
        ImGui::TextDisabled("%s", location.c_str());
      }
      ImGui::SameLine();
      ImGui::TextUnformatted(entry.message.c_str());
      ImGui::PopStyleColor();
      ImGui::PopID();
    }
    if (m_autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f) {
      ImGui::SetScrollHereY(1.0f);
    }
  }
  ImGui::EndChild();
  ImGui::End();
}

} // namespace sonnet::editor
