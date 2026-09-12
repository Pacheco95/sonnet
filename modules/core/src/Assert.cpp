#include <sonnet/core/Assert.h>

#include <sonnet/core/Log.h>

#include <cstdlib>

#if defined(_WIN32)
#include <intrin.h>
#define SONNET_DEBUG_BREAK() __debugbreak()
#else
#include <csignal>
#define SONNET_DEBUG_BREAK() ::raise(SIGTRAP)
#endif

namespace sonnet::core::detail {

void assertFailed(std::string_view module, std::string_view expression, std::string_view message,
                  std::source_location location) {
  spdlog::logger &logger = Log::get(module);
  logger.log(spdlog::source_loc{location.file_name(), static_cast<int>(location.line()), location.function_name()},
             spdlog::level::critical, "assertion failed: {}{}{}", expression, message.empty() ? "" : ": ", message);
  logger.flush();
  SONNET_DEBUG_BREAK();
  std::abort();
}

} // namespace sonnet::core::detail
