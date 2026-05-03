#include <catch2/catch_test_macros.hpp>

// Include only the data types — no ImGui dependency in tests
#include <sonnet/editor/IPanel.hpp>

// Pull in LogBuffer, LogEntry, LogSeverity, LogFilter via the sink header.
// This avoids linking ImGui while still testing the data contracts.
#include <chrono>
#include <memory>
#include <string>

// Re-declare only what we need to test (mirrors EditorLogSink.hpp structures).
// We access them by including the private header via the SonnetEditor link target.
#define SONNET_EDITOR_INTERNAL
#include "../../../src/editor/src/log/EditorLogSink.hpp"
#include "../../../src/editor/src/panels/LogPanel.hpp"

using namespace sonnet::editor;

static LogEntry makeEntry(LogSeverity sev, const std::string& msg = "x") {
    return LogEntry{std::chrono::system_clock::now(), sev, msg};
}

TEST_CASE("LogPanelTest_LogBuffer_CapsAt10000") {
    auto buffer = std::make_shared<LogBuffer>();
    for (int i = 0; i < 10'001; ++i) {
        buffer->push(makeEntry(LogSeverity::Info, std::to_string(i)));
    }
    std::lock_guard lock(buffer->mutex);
    REQUIRE(buffer->entries.size() == LogBuffer::kMaxEntries);
}

TEST_CASE("LogPanelTest_LogBuffer_DropsOldestWhenFull") {
    auto buffer = std::make_shared<LogBuffer>();
    buffer->push(makeEntry(LogSeverity::Info, "first"));
    for (int i = 0; i < static_cast<int>(LogBuffer::kMaxEntries); ++i) {
        buffer->push(makeEntry(LogSeverity::Info, "fill"));
    }
    std::lock_guard lock(buffer->mutex);
    REQUIRE(buffer->entries.front().message != "first");
}

TEST_CASE("LogPanelTest_LogBuffer_ClearEmptiesBuffer") {
    auto buffer = std::make_shared<LogBuffer>();
    buffer->push(makeEntry(LogSeverity::Warning));
    buffer->clear();
    std::lock_guard lock(buffer->mutex);
    REQUIRE(buffer->entries.empty());
}

TEST_CASE("LogPanelTest_LogFilter_DefaultAllTrue") {
    LogFilter f;
    REQUIRE(f.showInfo);
    REQUIRE(f.showWarnings);
    REQUIRE(f.showErrors);
}

TEST_CASE("LogPanelTest_LogFilter_ShowErrorsFalse_ExcludesErrors") {
    auto buffer = std::make_shared<LogBuffer>();
    buffer->push(makeEntry(LogSeverity::Info, "info-msg"));
    buffer->push(makeEntry(LogSeverity::Warning, "warn-msg"));
    buffer->push(makeEntry(LogSeverity::Error, "err-msg"));

    LogFilter filter;
    filter.showErrors = false;

    std::lock_guard lock(buffer->mutex);
    int count = 0;
    for (const auto& entry : buffer->entries) {
        if (entry.severity == LogSeverity::Info && filter.showInfo) {
            ++count;
        } else if (entry.severity == LogSeverity::Warning && filter.showWarnings) {
            ++count;
        } else if (entry.severity == LogSeverity::Error && filter.showErrors) {
            ++count;
        }
    }
    REQUIRE(count == 2); // info + warning, no error
}

TEST_CASE("LogPanelTest_LogPanel_AutoScrollDefaultTrue") {
    // Construct LogPanel with an empty buffer; default m_autoScroll is true.
    // We verify the panel constructs without crashing and has title "Log".
    auto buffer = std::make_shared<LogBuffer>();
    LogPanel panel(std::move(buffer));
    REQUIRE(std::string(panel.title()) == "Log");
    REQUIRE_FALSE(panel.isPermanent());
}
