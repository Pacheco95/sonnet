#pragma once

#include <sonnet/platform/Event.h>
#include <sonnet/platform/Platform.h>

#include <memory>
#include <span>
#include <string_view>

namespace sonnet::platform {

enum class AppResult {
  Continue,
  Success,
  Failure,
};

// The application callback interface behind SDL3's callback main loop. On desktop the platform
// calls iterate in a loop; on mobile the OS drives it. Frame ordering lives in the implementation.
class IApplication {
public:
  virtual ~IApplication() = default;

  virtual AppResult iterate() = 0;
  virtual AppResult event(const Event &event) = 0;
};

// Defined by the executable, which includes <sonnet/platform/EntryPoint.h> exactly once. Called
// after the platform is up; may throw, which terminates with the message logged.
std::unique_ptr<IApplication> createApplication(Platform &platform, std::span<const std::string_view> args);

} // namespace sonnet::platform
