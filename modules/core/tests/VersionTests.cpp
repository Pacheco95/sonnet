#include <sonnet/core/Version.h>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("engine version matches the milestone scheme", "[core][version]") {
  const auto version = sonnet::core::engineVersion();
  REQUIRE(version.major == 0);
  REQUIRE(version.minor >= 1);
  // M1 landed as 0.2.0 (docs/conventions.md, "Versioning").
  REQUIRE(version.toString() == "0.3.0");
}

TEST_CASE("versions order lexicographically", "[core][version]") {
  using sonnet::core::Version;
  STATIC_REQUIRE(Version{0, 1, 0} < Version{0, 2, 0});
  STATIC_REQUIRE(Version{0, 9, 9} < Version{1, 0, 0});
  STATIC_REQUIRE(Version{1, 0, 0} == Version{1, 0, 0});
}
