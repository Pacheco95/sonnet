#include <sonnet/core/Error.h>

#include <catch2/catch_test_macros.hpp>

#include <expected>
#include <string>

namespace {

sonnet::core::Result<int> parsePositive(int value) {
  if (value <= 0) {
    return std::unexpected(sonnet::core::Error{"value must be positive", sonnet::core::ErrorCategory::Io});
  }
  return value;
}

} // namespace

TEST_CASE("Error captures the creation site", "[core][error]") {
  const auto result = parsePositive(-1);
  REQUIRE(!result.has_value());
  const sonnet::core::Error &error = result.error();
  REQUIRE(error.message == "value must be positive");
  REQUIRE(error.category == sonnet::core::ErrorCategory::Io);
  REQUIRE(std::string{error.location.file_name()}.ends_with("ErrorTests.cpp"));
  REQUIRE(std::string{error.location.function_name()}.contains("parsePositive"));
  REQUIRE(error.toString().contains("value must be positive (Io, ErrorTests.cpp:"));
}

TEST_CASE("Result carries values", "[core][error]") {
  REQUIRE(parsePositive(3).value() == 3);
}

TEST_CASE("Exception reports the throw site in what()", "[core][error]") {
  try {
    throw sonnet::core::Exception{"device lost", sonnet::core::ErrorCategory::Graphics};
  } catch (const sonnet::core::Exception &e) {
    REQUIRE(std::string{e.what()}.starts_with("device lost (Graphics, ErrorTests.cpp:"));
    REQUIRE(e.error().category == sonnet::core::ErrorCategory::Graphics);
    return;
  }
  FAIL("exception was not caught");
}

TEST_CASE("Exception is a std::exception", "[core][error]") {
  try {
    throw sonnet::core::Exception{sonnet::core::Error{"from error"}};
  } catch (const std::exception &e) {
    REQUIRE(std::string{e.what()}.starts_with("from error (Unknown"));
  }
}
