#include <sonnet/core/Version.h>

#include <format>

namespace sonnet::core {

std::string Version::toString() const {
  return std::format("{}.{}.{}", major, minor, patch);
}

Version engineVersion() noexcept {
  return Version{SONNET_VERSION_MAJOR, SONNET_VERSION_MINOR, SONNET_VERSION_PATCH};
}

} // namespace sonnet::core
