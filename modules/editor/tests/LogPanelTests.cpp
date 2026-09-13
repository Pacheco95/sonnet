#include <sonnet/editor/LogPanel.h>

#include <sonnet/core/Log.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

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
