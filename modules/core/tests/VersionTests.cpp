#include <sonnet/core/Version.h>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("engine version matches the milestone scheme", "[core][version]") {
  const auto version = sonnet::core::engineVersion();
  REQUIRE(version.major == 0);
  REQUIRE(version.minor >= 1);
  // Each milestone bumps MINOR, and so does a feature or breaking change between them: M8 landed
  // as 0.9.0 and the macOS fixes as 0.10.0 (docs/conventions.md, "Versioning"). The pin is
  // deliberate, so that landing a milestone without bumping the version fails here.
  REQUIRE(version.toString() == "0.10.0");
}

TEST_CASE("versions order lexicographically", "[core][version]") {
  using sonnet::core::Version;
  STATIC_REQUIRE(Version{0, 1, 0} < Version{0, 2, 0});
  STATIC_REQUIRE(Version{0, 9, 9} < Version{1, 0, 0});
  STATIC_REQUIRE(Version{1, 0, 0} == Version{1, 0, 0});
}
