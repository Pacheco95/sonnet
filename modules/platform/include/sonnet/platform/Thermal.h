#pragma once

#include <string_view>

namespace sonnet::platform {

// How hard the system says it is working to shed heat (docs/platform.md, "Thermal state"). Four
// levels, the ones iOS names; Android's finer ones fold into them. Unknown is the answer where the
// system gives none, and is not Nominal: a run that cannot tell has not been shown to stay cool.
enum class ThermalState {
  Unknown,
  Nominal,
  Fair,
  Serious,
  Critical
};

// The system's present state; cheap enough to call once a second. Unknown on Linux and Windows.
[[nodiscard]] ThermalState thermalState() noexcept;

[[nodiscard]] constexpr std::string_view toString(ThermalState state) noexcept {
  switch (state) {
  case ThermalState::Unknown:
    return "n/a";
  case ThermalState::Nominal:
    return "nominal";
  case ThermalState::Fair:
    return "fair";
  case ThermalState::Serious:
    return "serious";
  case ThermalState::Critical:
    return "critical";
  }
  return "n/a";
}

} // namespace sonnet::platform
