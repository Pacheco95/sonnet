#include <sonnet/core/Log.h>

#include <catch2/catch_test_macros.hpp>
#include <spdlog/sinks/ostream_sink.h>

#include <memory>
#include <sstream>
#include <string>

namespace {

struct CaptureSink {
  std::ostringstream stream;
  spdlog::sink_ptr sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(stream, true);

  CaptureSink() {
    sonnet::core::Log::addSink(sink);
  }
  ~CaptureSink() {
    sonnet::core::Log::removeSink(sink);
  }
  CaptureSink(const CaptureSink &) = delete;
  CaptureSink &operator=(const CaptureSink &) = delete;
};

} // namespace

TEST_CASE("log lines carry module, level, file and line", "[core][log]") {
  CaptureSink capture;
  SONNET_LOG_INFO("hello {}", 42);
  const int line = __LINE__ - 1;
  const std::string text = capture.stream.str();
  REQUIRE(text.contains("[info] [core_tests] [LogTests.cpp:" + std::to_string(line) + "] hello 42"));
}

TEST_CASE("located records carry the given location and a runtime level", "[core][log]") {
  CaptureSink capture;
  const spdlog::source_loc location{"/project/scripts/mover.lua", 7, "lua"};
  const spdlog::level::level_enum level = spdlog::level::err;
  SONNET_LOG_LOCATED(level, location, "bad {}", "value");
  REQUIRE(capture.stream.str().contains("[error] [core_tests] [mover.lua:7] bad value"));
}

TEST_CASE("loggers are shared per module", "[core][log]") {
  REQUIRE(&sonnet::core::Log::get("rhi") == &sonnet::core::Log::get("rhi"));
  REQUIRE(&sonnet::core::Log::get("rhi") != &sonnet::core::Log::get("core"));
  REQUIRE(sonnet::core::Log::get("rhi").name() == "rhi");
}

TEST_CASE("sinks added later receive records from existing loggers", "[core][log]") {
  spdlog::logger &logger = sonnet::core::Log::get("platform");
  CaptureSink capture;
  logger.log(spdlog::source_loc{"Window.cpp", 12, "open"}, spdlog::level::warn, "late sink");
  REQUIRE(capture.stream.str().contains("[warn] [platform] [Window.cpp:12] late sink"));
}

TEST_CASE("level filters records", "[core][log]") {
  CaptureSink capture;
  sonnet::core::Log::setLevel(spdlog::level::warn);
  SONNET_LOG_INFO("filtered");
  SONNET_LOG_WARN("kept");
  sonnet::core::Log::setLevel(spdlog::level::debug);
  const std::string text = capture.stream.str();
  REQUIRE(!text.contains("filtered"));
  REQUIRE(text.contains("kept"));
}
