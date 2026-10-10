#include <sonnet/platform/Thermal.h>

#ifdef __ANDROID__
#include <android/thermal.h>
#endif

namespace sonnet::platform {

#ifdef __ANDROID__

ThermalState thermalState() noexcept {
  // The manager lives as long as the process; AThermal_acquireManager is the only way to a status.
  static AThermalManager *const manager = AThermal_acquireManager();
  if (manager == nullptr) {
    return ThermalState::Unknown;
  }
  switch (AThermal_getCurrentThermalStatus(manager)) {
  case ATHERMAL_STATUS_NONE:
    return ThermalState::Nominal;
  case ATHERMAL_STATUS_LIGHT:
  case ATHERMAL_STATUS_MODERATE:
    return ThermalState::Fair;
  case ATHERMAL_STATUS_SEVERE:
    return ThermalState::Serious;
  case ATHERMAL_STATUS_CRITICAL:
  case ATHERMAL_STATUS_EMERGENCY:
  case ATHERMAL_STATUS_SHUTDOWN:
    return ThermalState::Critical;
  default:
    return ThermalState::Unknown;
  }
}

#else

ThermalState thermalState() noexcept {
  return ThermalState::Unknown;
}

#endif

} // namespace sonnet::platform
