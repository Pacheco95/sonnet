#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace sonnet::core {

// 128-bit identifier. Asset references are UUIDs, never paths, so files can move without
// breaking references. Generated identifiers are random (RFC 9562 version 4).
class Uuid {
public:
  using Bytes = std::array<std::uint8_t, 16>;

  constexpr Uuid() noexcept = default;
  constexpr explicit Uuid(const Bytes &bytes) noexcept : m_bytes(bytes) {
  }

  [[nodiscard]] static Uuid generate();
  // A name-based identifier (RFC 9562 version 8) that is the same for the same parent and name
  // every time: what a sub-asset such as a glTF file's mesh keeps across imports.
  [[nodiscard]] static Uuid derive(const Uuid &parent, std::string_view name) noexcept;
  // Accepts the canonical 8-4-4-4-12 hexadecimal form, either case, with or without braces.
  [[nodiscard]] static std::optional<Uuid> parse(std::string_view text) noexcept;

  [[nodiscard]] constexpr bool isNil() const noexcept {
    return m_bytes == Bytes{};
  }
  [[nodiscard]] constexpr const Bytes &bytes() const noexcept {
    return m_bytes;
  }
  // Lowercase 8-4-4-4-12 form.
  [[nodiscard]] std::string toString() const;

  constexpr auto operator<=>(const Uuid &) const noexcept = default;

private:
  Bytes m_bytes{};
};

} // namespace sonnet::core

template <> struct std::hash<sonnet::core::Uuid> {
  std::size_t operator()(const sonnet::core::Uuid &uuid) const noexcept {
    std::uint64_t low = 0;
    std::uint64_t high = 0;
    const auto &bytes = uuid.bytes();
    for (std::size_t i = 0; i < 8; ++i) {
      low = (low << 8) | bytes[i];
      high = (high << 8) | bytes[i + 8];
    }
    return std::hash<std::uint64_t>{}(low ^ (high * 0x9E3779B97F4A7C15ull));
  }
};
