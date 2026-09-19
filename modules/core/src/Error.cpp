#include <sonnet/core/Error.h>

#include <filesystem>
#include <format>

namespace sonnet::core {

std::string_view toString(ErrorCategory category) noexcept {
  switch (category) {
  case ErrorCategory::Unknown:
    return "Unknown";
  case ErrorCategory::Platform:
    return "Platform";
  case ErrorCategory::Graphics:
    return "Graphics";
  case ErrorCategory::Shader:
    return "Shader";
  case ErrorCategory::Io:
    return "Io";
  case ErrorCategory::Script:
    return "Script";
  }
  return "Unknown";
}

std::string Error::toString() const {
  return std::format("{} ({}, {}:{})", message, core::toString(category),
                     std::filesystem::path{location.file_name()}.filename().string(), location.line());
}

Exception::Exception(std::string message, ErrorCategory category, std::source_location location)
    : Exception(Error{std::move(message), category, location}) {
}

Exception::Exception(Error error) : m_error(std::move(error)), m_what(m_error.toString()) {
}

const char *Exception::what() const noexcept {
  return m_what.c_str();
}

} // namespace sonnet::core
