#pragma once

#include <imgui.h>
#include <spdlog/common.h>
#include <spdlog/sinks/sink.h>

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace sonnet::editor {

struct LogEntry {
  spdlog::level::level_enum level{spdlog::level::info};
  std::string time; // HH:MM:SS.mmm
  std::string module;
  std::string file; // base name
  std::string path; // as recorded: repository-relative for engine sources
  int line{0};
  std::string message;
  std::uint64_t sequence{0}; // numbers the buffer's entries in arrival order, so a row keeps its identity
};

// One entry as a line of text: `time [level] [module] file:line message`, the location left out
// when the record has none. This is what the panel copies.
[[nodiscard]] std::string formatLogEntry(const LogEntry &entry);

// A sink attached to core::Log that keeps the newest entries for the panel. Records arrive
// from any thread; the panel reads a snapshot.
class LogBuffer final : public spdlog::sinks::sink {
public:
  static constexpr std::size_t Capacity = 4000;

  void log(const spdlog::details::log_msg &msg) override;
  void flush() override {
  }
  void set_pattern(const std::string &) override {
  }
  void set_formatter(std::unique_ptr<spdlog::formatter>) override {
  }

  [[nodiscard]] std::vector<LogEntry> snapshot() const;
  [[nodiscard]] std::uint64_t revision() const;
  void clear();

private:
  mutable std::mutex m_mutex;
  std::deque<LogEntry> m_entries;
  std::uint64_t m_revision{0};
  std::uint64_t m_sequence{1};
};

// The editor's log panel (docs/conventions.md, "Logging"): entries coloured by level, a level
// threshold and a text filter, auto-scroll, rows selected like a list (click, Shift, Ctrl, Ctrl+A)
// and copied as text with Ctrl+C or the context menu. The sink is registered with core::Log for
// the panel's lifetime.
class LogPanel {
public:
  LogPanel();
  ~LogPanel();
  LogPanel(const LogPanel &) = delete;
  LogPanel &operator=(const LogPanel &) = delete;

  void draw(bool &open);
  // Makes every file:line a link that calls back with the recorded path and line.
  void setLocationHandler(std::function<void(const std::string &path, int line)> handler) {
    m_openLocation = std::move(handler);
  }

  [[nodiscard]] const LogBuffer &buffer() const noexcept {
    return *m_buffer;
  }

  // The selected rows that pass the level and text filters, one formatLogEntry line each, in
  // arrival order; copySelection puts them on the clipboard, copyAll every row that passes.
  [[nodiscard]] std::string selectionText() const;
  void copySelection() const;
  void copyAll() const;

private:
  [[nodiscard]] bool visible(const LogEntry &entry) const;
  void drawContextMenu();

  std::shared_ptr<LogBuffer> m_buffer;
  std::function<void(const std::string &, int)> m_openLocation;
  std::vector<LogEntry> m_entries;
  std::uint64_t m_revision{0};
  int m_minimumLevel{static_cast<int>(spdlog::level::trace)};
  std::string m_filter;
  bool m_autoScroll{true};
  ImGuiSelectionBasicStorage m_selection; // keyed by LogEntry::sequence
  std::vector<const LogEntry *> m_rows;   // the rows drawn this frame, for the selection's index
};

} // namespace sonnet::editor
