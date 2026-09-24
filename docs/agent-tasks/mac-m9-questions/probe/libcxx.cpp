// ADR-0018, open question 3: which iOS deployment target the library features the player uses
// need. Compiled once per feature (-DFEATURE_...) and target with -fsyntax-only; Apple's libc++
// marks a feature the target OS's dylib lacks as unavailable, which is a compile error.
#include <atomic>
#include <charconv>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <format>
#include <print>
#include <string>

int main() {
#if defined(FEATURE_FORMAT_DOUBLE) // std::format with a floating-point argument; 40 engine files use std::format
  std::string s = std::format("{:.3f}", 1.5);
#elif defined(FEATURE_TO_CHARS_FLOAT)
  char buffer[32];
  auto result = std::to_chars(buffer, buffer + sizeof buffer, 1.5f);
  (void)result;
#elif defined(FEATURE_FROM_CHARS_FLOAT) // the editor's --play parser, which ADR-0018 moves into the player
  float value = 0.0f;
  const char text[] = "3.5";
  auto result = std::from_chars(text, text + 3, value);
  (void)result;
#elif defined(FEATURE_FROM_CHARS_INT) // scripting's line parser, the --settle-frames parser
  unsigned value = 0;
  const char text[] = "10";
  auto result = std::from_chars(text, text + 2, value);
  (void)result;
#elif defined(FEATURE_PRINT) // only apps/editor and apps/cook use std::print today
  std::println("{}", 1);
#elif defined(FEATURE_ATOMIC_WAIT) // core's job system
  std::atomic<int> flag{0};
  flag.wait(1);
  flag.notify_all();
#elif defined(FEATURE_FILESYSTEM)
  auto path = std::filesystem::temp_directory_path();
  (void)path;
#elif defined(FEATURE_EXPECTED)
  std::expected<int, int> value = std::unexpected(1);
  (void)value.value_or(0);
#else
#error define one FEATURE_ macro
#endif
  return 0;
}
