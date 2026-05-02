#include <sonnet/logging/Logger.hpp>

#include <spdlog/sinks/stdout_color_sinks.h>

#include <memory>
#include <vector>

namespace sonnet::logging {

void init() {
    static bool initialized = false;
    if (initialized) {
        return;
    }
    initialized = true;

    constexpr auto kPattern = "[%H:%M:%S.%e] [%^%l%$] [%s:%#] %v";

    auto stdoutSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    stdoutSink->set_level(spdlog::level::trace);
    stdoutSink->set_pattern(kPattern);

    auto stderrSink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
    stderrSink->set_level(spdlog::level::warn);
    stderrSink->set_pattern(kPattern);

    auto logger =
        std::make_shared<spdlog::logger>("sonnet", spdlog::sinks_init_list{stdoutSink, stderrSink});
    logger->set_level(spdlog::level::trace);
    spdlog::set_default_logger(logger);

    SONNET_LOG_INFO("Sonnet logging initialized");
}

} // namespace sonnet::logging
