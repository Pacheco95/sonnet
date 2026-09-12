#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace sonnet::core {

// Typed opaque handle: a 32-bit slot index plus a 32-bit generation. The tag makes handles of
// different resource types distinct at compile time; the generation lets the owning pool detect a
// stale handle after the slot was reused instead of silently aliasing a new resource.
template <typename Tag> struct Handle {
  static constexpr std::uint32_t InvalidIndex = 0xFFFFFFFFu;

  std::uint32_t index{InvalidIndex};
  std::uint32_t generation{0};

  [[nodiscard]] constexpr bool isValid() const noexcept {
    return index != InvalidIndex;
  }
  constexpr explicit operator bool() const noexcept {
    return isValid();
  }

  // Packed form for storage in GPU buffers, maps and logs.
  [[nodiscard]] constexpr std::uint64_t packed() const noexcept {
    return (static_cast<std::uint64_t>(generation) << 32) | index;
  }
  [[nodiscard]] static constexpr Handle fromPacked(std::uint64_t value) noexcept {
    return Handle{static_cast<std::uint32_t>(value & 0xFFFFFFFFu), static_cast<std::uint32_t>(value >> 32)};
  }

  constexpr auto operator<=>(const Handle &) const noexcept = default;
};

} // namespace sonnet::core

template <typename Tag> struct std::hash<sonnet::core::Handle<Tag>> {
  std::size_t operator()(const sonnet::core::Handle<Tag> &handle) const noexcept {
    return std::hash<std::uint64_t>{}(handle.packed());
  }
};
