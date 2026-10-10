#include <sonnet/platform/Thermal.h>

#include <catch2/catch_test_macros.hpp>

using sonnet::platform::ThermalState;

TEST_CASE("thermal states have names and unknown reads as n/a", "[platform][thermal]") {
  REQUIRE(toString(ThermalState::Unknown) == "n/a");
  REQUIRE(toString(ThermalState::Nominal) == "nominal");
  REQUIRE(toString(ThermalState::Fair) == "fair");
  REQUIRE(toString(ThermalState::Serious) == "serious");
  REQUIRE(toString(ThermalState::Critical) == "critical");
}

TEST_CASE("the system's thermal state can be read", "[platform][thermal]") {
  const ThermalState state = sonnet::platform::thermalState();
#if defined(__linux__) && !defined(__ANDROID__) || defined(_WIN32)
  REQUIRE(state == ThermalState::Unknown);
#else
  // Apple and Android name a state; a desktop on mains power is not throttling in a test run, but
  // the machine may be hot, so only that a level came back is checked.
  REQUIRE(state != ThermalState::Unknown);
#endif
}
