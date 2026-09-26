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

// Named only inside sizeof, never called: a compiled-out assertion still refers to its operands, so a
// value that only an assertion reads is not reported as unused in Release.
template <class... Args> bool unevaluated(const Args &...) noexcept;

} // namespace sonnet::core::detail

#define SONNET_DETAIL_ASSERT_FAIL(expr, ...)                                                                           \
  ::sonnet::core::detail::assertFailed(SONNET_MODULE, #expr, ::std::string{__VA_OPT__(::std::format(__VA_ARGS__))},    \
                                       ::std::source_location::current())

#define SONNET_DETAIL_ASSERT_UNEVALUATED(...)                                                                          \
  static_cast<void>(sizeof(::sonnet::core::detail::unevaluated(__VA_ARGS__)))

// Active in Debug and RelWithDebInfo, compiled out (expression and message not evaluated) in Release.
#if SONNET_ASSERTS_ENABLED
#define SONNET_ASSERT(expr, ...)                                                                                       \
  do {                                                                                                                 \
    if (!(expr)) [[unlikely]] {                                                                                        \
      SONNET_DETAIL_ASSERT_FAIL(expr, __VA_ARGS__);                                                                    \
    }                                                                                                                  \
  } while (false)
#else
#define SONNET_ASSERT(expr, ...) SONNET_DETAIL_ASSERT_UNEVALUATED(!(expr)__VA_OPT__(, ) __VA_ARGS__)
#endif

// Always evaluates the expression; only the check compiles out in Release.
#if SONNET_ASSERTS_ENABLED
#define SONNET_VERIFY(expr, ...) SONNET_ASSERT(expr, __VA_ARGS__)
#else
#define SONNET_VERIFY(expr, ...)                                                                                       \
  do {                                                                                                                 \
    static_cast<void>(expr);                                                                                           \
    SONNET_DETAIL_ASSERT_UNEVALUATED(true __VA_OPT__(, ) __VA_ARGS__);                                                 \
  } while (false)
#endif
