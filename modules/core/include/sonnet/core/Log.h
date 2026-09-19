#pragma once

#include <spdlog/spdlog.h>

#include <string_view>

#ifndef SONNET_MODULE
#error "SONNET_MODULE must name the module or app being compiled; sonnet_add_module sets it"
#endif

namespace sonnet::core {

// One spdlog logger per module, all sharing the same sinks. Loggers are created on first use so
// modules need no registration step; sinks added later reach every logger, existing or future.
class Log {
public:
  // Idempotent. Installs the console sink and the line format. Called automatically on first use.
  static void init();
  // Flushes every logger. Loggers stay usable afterwards; nothing is torn down.
  static void flush();

  [[nodiscard]] static spdlog::logger &get(std::string_view module);

  static void addSink(spdlog::sink_ptr sink);
  static void removeSink(const spdlog::sink_ptr &sink);
  static void setLevel(spdlog::level::level_enum level);
};

} // namespace sonnet::core

// Every log call goes through these so the record carries the calling file, line and function.
// Trace and debug compile out of Release builds through SPDLOG_ACTIVE_LEVEL, set by the build; the
// compiled-out form keeps the call inside a discarded branch so the arguments stay used and the
// format string stays checked in every configuration.
#define SONNET_LOG_AT(lvl, ...)                                                                                        \
  ::sonnet::core::Log::get(SONNET_MODULE)                                                                              \
      .log(::spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, ::spdlog::level::lvl, __VA_ARGS__)

// For records whose origin is not a C++ line, such as a script's (docs/conventions.md, "Logging"):
// the level is a spdlog::level::level_enum value and the location is given, not captured. The
// location's strings must outlive the call; sinks copy what they keep.
#define SONNET_LOG_LOCATED(level, location, ...)                                                                       \
  ::sonnet::core::Log::get(SONNET_MODULE).log(location, level, __VA_ARGS__)

#define SONNET_LOG_COMPILED_OUT(lvl, ...)                                                                              \
  do {                                                                                                                 \
    if constexpr (false) {                                                                                             \
      SONNET_LOG_AT(lvl, __VA_ARGS__);                                                                                 \
    }                                                                                                                  \
  } while (false)

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
#define SONNET_LOG_TRACE(...) SONNET_LOG_AT(trace, __VA_ARGS__)
#else
#define SONNET_LOG_TRACE(...) SONNET_LOG_COMPILED_OUT(trace, __VA_ARGS__)
#endif

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_DEBUG
#define SONNET_LOG_DEBUG(...) SONNET_LOG_AT(debug, __VA_ARGS__)
#else
#define SONNET_LOG_DEBUG(...) SONNET_LOG_COMPILED_OUT(debug, __VA_ARGS__)
#endif

#define SONNET_LOG_INFO(...) SONNET_LOG_AT(info, __VA_ARGS__)
#define SONNET_LOG_WARN(...) SONNET_LOG_AT(warn, __VA_ARGS__)
#define SONNET_LOG_ERROR(...) SONNET_LOG_AT(err, __VA_ARGS__)
#define SONNET_LOG_CRITICAL(...) SONNET_LOG_AT(critical, __VA_ARGS__)
