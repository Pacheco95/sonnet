#pragma once

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
};

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
};

// The editor's log panel (docs/conventions.md, "Logging"): entries coloured by level, a level
// threshold and a text filter, auto-scroll. The sink is registered with core::Log for the
// panel's lifetime.
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

private:
  std::shared_ptr<LogBuffer> m_buffer;
  std::function<void(const std::string &, int)> m_openLocation;
  std::vector<LogEntry> m_entries;
  std::uint64_t m_revision{0};
  int m_minimumLevel{static_cast<int>(spdlog::level::trace)};
  std::string m_filter;
  bool m_autoScroll{true};
};

} // namespace sonnet::editor
