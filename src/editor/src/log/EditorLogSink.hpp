#pragma once

#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <spdlog/sinks/base_sink.h>

namespace sonnet::editor {

enum class LogSeverity { Info, Warning, Error };

struct LogEntry {
    std::chrono::system_clock::time_point timestamp;
    LogSeverity severity;
    std::string message;
};

struct LogBuffer {
    std::deque<LogEntry> entries;
    mutable std::mutex mutex;
    static constexpr std::size_t kMaxEntries = 10'000;

    void push(LogEntry entry) {
        std::lock_guard lock(mutex);
        if (entries.size() >= kMaxEntries) {
            entries.pop_front();
        }
        entries.push_back(std::move(entry));
    }

    void clear() {
        std::lock_guard lock(mutex);
        entries.clear();
    }
};

class EditorLogSink : public spdlog::sinks::base_sink<std::mutex> {
public:
    explicit EditorLogSink(std::shared_ptr<LogBuffer> buffer);

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override;
    void flush_() override {}

private:
    std::shared_ptr<LogBuffer> m_buffer;
};

} // namespace sonnet::editor
