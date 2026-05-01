#pragma once

#include <spdlog/spdlog.h>

namespace sonnet::logging {
void init();
} // namespace sonnet::logging

#define SONNET_LOG_TRACE(...) SPDLOG_TRACE(__VA_ARGS__)
#define SONNET_LOG_DEBUG(...) SPDLOG_DEBUG(__VA_ARGS__)
#define SONNET_LOG_INFO(...) SPDLOG_INFO(__VA_ARGS__)
#define SONNET_LOG_WARN(...) SPDLOG_WARN(__VA_ARGS__)
#define SONNET_LOG_ERROR(...) SPDLOG_ERROR(__VA_ARGS__)
#define SONNET_LOG_CRITICAL(...) SPDLOG_CRITICAL(__VA_ARGS__)
