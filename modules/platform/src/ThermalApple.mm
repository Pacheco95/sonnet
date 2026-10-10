#include <sonnet/platform/Thermal.h>

#import <Foundation/Foundation.h>

namespace sonnet::platform {

ThermalState thermalState() noexcept {
  switch ([NSProcessInfo processInfo].thermalState) {
  case NSProcessInfoThermalStateNominal:
    return ThermalState::Nominal;
  case NSProcessInfoThermalStateFair:
    return ThermalState::Fair;
  case NSProcessInfoThermalStateSerious:
    return ThermalState::Serious;
  case NSProcessInfoThermalStateCritical:
    return ThermalState::Critical;
  }
  return ThermalState::Unknown;
}

} // namespace sonnet::platform
