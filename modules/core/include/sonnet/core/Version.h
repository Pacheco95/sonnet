#pragma once

#include <compare>
#include <cstdint>
#include <string>

namespace sonnet::core {

struct Version {
  std::uint32_t major{0};
  std::uint32_t minor{0};
  std::uint32_t patch{0};

  constexpr auto operator<=>(const Version &) const noexcept = default;
  [[nodiscard]] std::string toString() const;
};

// The version declared in the root CMakeLists.txt, the single source of truth.
[[nodiscard]] Version engineVersion() noexcept;

} // namespace sonnet::core
