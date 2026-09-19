#pragma once

#include <exception>
#include <expected>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>

namespace sonnet::core {

enum class ErrorCategory {
  Unknown,
  Platform,
  Graphics,
  Shader,
  Io,
  Script,
};

[[nodiscard]] std::string_view toString(ErrorCategory category) noexcept;

// Value returned by recoverable operations. Aggregate-initialise it, `Error{"message", category}`:
// the default member initialiser evaluates std::source_location::current() at the initialisation
// site, so the line that reports the error shows where the problem originated, not where it was
// logged.
struct Error {
  std::string message;
  ErrorCategory category{ErrorCategory::Unknown};
  std::source_location location = std::source_location::current();

  // "message (Category, File.cpp:42)". The file is repository-relative when the build maps it.
  [[nodiscard]] std::string toString() const;
};

template <typename T> using Result = std::expected<T, Error>;

// Base of every engine exception. Thrown by initialisation and resource creation only; the
// per-frame path never throws.
class Exception : public std::exception {
public:
  explicit Exception(std::string message, ErrorCategory category = ErrorCategory::Unknown,
                     std::source_location location = std::source_location::current());
  explicit Exception(Error error);

  [[nodiscard]] const char *what() const noexcept override;
  [[nodiscard]] const Error &error() const noexcept {
    return m_error;
  }

private:
  Error m_error;
  std::string m_what;
};

} // namespace sonnet::core
