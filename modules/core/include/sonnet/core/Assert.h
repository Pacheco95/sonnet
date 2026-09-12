#pragma once

#include <format>
#include <source_location>
#include <string>
#include <string_view>

namespace sonnet::core::detail {

// Logs the expression and the failure site, breaks into the debugger when one is attached and
// terminates. Assertions are for programmer errors, never for bad input data.
[[noreturn]] void assertFailed(std::string_view module, std::string_view expression, std::string_view message,
                               std::source_location location);

} // namespace sonnet::core::detail

#define SONNET_DETAIL_ASSERT_FAIL(expr, ...)                                                                           \
  ::sonnet::core::detail::assertFailed(SONNET_MODULE, #expr, ::std::string{__VA_OPT__(::std::format(__VA_ARGS__))},    \
                                       ::std::source_location::current())

// Active in Debug and RelWithDebInfo, compiled out (expression not evaluated) in Release.
#if SONNET_ASSERTS_ENABLED
#define SONNET_ASSERT(expr, ...)                                                                                       \
  do {                                                                                                                 \
    if (!(expr)) [[unlikely]] {                                                                                        \
      SONNET_DETAIL_ASSERT_FAIL(expr, __VA_ARGS__);                                                                    \
    }                                                                                                                  \
  } while (false)
#else
#define SONNET_ASSERT(expr, ...) static_cast<void>(0)
#endif

// Always evaluates the expression; only the check compiles out in Release.
#if SONNET_ASSERTS_ENABLED
#define SONNET_VERIFY(expr, ...) SONNET_ASSERT(expr, __VA_ARGS__)
#else
#define SONNET_VERIFY(expr, ...) static_cast<void>(expr)
#endif
