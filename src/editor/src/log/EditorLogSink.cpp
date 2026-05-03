#include "EditorLogSink.hpp"

#include <spdlog/details/log_msg.h>

namespace sonnet::editor {

EditorLogSink::EditorLogSink(std::shared_ptr<LogBuffer> buffer) : m_buffer(std::move(buffer)) {}

void EditorLogSink::sink_it_(const spdlog::details::log_msg& msg) {
    LogSeverity severity = LogSeverity::Info;
    if (msg.level == spdlog::level::warn) {
        severity = LogSeverity::Warning;
    } else if (msg.level >= spdlog::level::err) {
        severity = LogSeverity::Error;
    }

    spdlog::memory_buf_t formatted;
    base_sink<std::mutex>::formatter_->format(msg, formatted);

    m_buffer->push(LogEntry{
        .timestamp = std::chrono::system_clock::now(),
        .severity = severity,
        .message = std::string(formatted.data(), formatted.size()),
    });
}

} // namespace sonnet::editor
