#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace sonnet::assets::detail {

// The little machinery the cooked payloads are written and read with (docs/assets.md, "Cooking
// and export"). Little-endian, tightly packed, no alignment assumptions: every read goes
// through memcpy. Every platform the engine targets is little-endian, and a bundle is cooked
// per platform anyway, so no byte swapping is done.
static_assert(std::endian::native == std::endian::little, "cooked payloads are little-endian");

class ByteWriter {
public:
  void u32(std::uint32_t value) {
    write(&value, sizeof(value));
  }
  void u64(std::uint64_t value) {
    write(&value, sizeof(value));
  }
  void f32(float value) {
    write(&value, sizeof(value));
  }
  void tag(std::string_view fourCharacters) {
    write(fourCharacters.data(), 4);
  }
  template <typename T>
  void pod(const T &value)
    requires std::is_trivially_copyable_v<T>
  {
    write(&value, sizeof(value));
  }
  // A count and then the elements, which is what `array` reads back.
  template <typename T>
  void array(std::span<const T> values)
    requires std::is_trivially_copyable_v<T>
  {
    u32(static_cast<std::uint32_t>(values.size()));
    write(values.data(), values.size_bytes());
  }
  void string(std::string_view text) {
    u32(static_cast<std::uint32_t>(text.size()));
    write(text.data(), text.size());
  }

  [[nodiscard]] std::vector<std::byte> take() noexcept {
    return std::move(m_bytes);
  }

private:
  void write(const void *data, std::size_t size) {
    const auto *bytes = static_cast<const std::byte *>(data);
    m_bytes.insert(m_bytes.end(), bytes, bytes + size);
  }

  std::vector<std::byte> m_bytes;
};

// Reads what ByteWriter wrote. A read past the end fails the reader and yields a zeroed value,
// so a truncated or foreign payload turns into one `ok()` check at the end rather than a crash.
class ByteReader {
public:
  explicit ByteReader(std::span<const std::byte> bytes) noexcept : m_bytes(bytes) {
  }

  [[nodiscard]] bool ok() const noexcept {
    return m_ok;
  }
  [[nodiscard]] std::size_t remaining() const noexcept {
    return m_bytes.size() - m_cursor;
  }

  [[nodiscard]] std::uint32_t u32() {
    std::uint32_t value = 0;
    read(&value, sizeof(value));
    return value;
  }
  [[nodiscard]] std::uint64_t u64() {
    std::uint64_t value = 0;
    read(&value, sizeof(value));
    return value;
  }
  [[nodiscard]] float f32() {
    float value = 0.0f;
    read(&value, sizeof(value));
    return value;
  }
  // True when the next four bytes are the tag, which is how a payload of the wrong kind is
  // caught before anything is decoded from it.
  [[nodiscard]] bool tag(std::string_view fourCharacters) {
    std::array<char, 4> read4{};
    read(read4.data(), read4.size());
    if (!m_ok || std::string_view{read4.data(), read4.size()} != fourCharacters) {
      m_ok = false;
      return false;
    }
    return true;
  }
  template <typename T>
  [[nodiscard]] T pod()
    requires std::is_trivially_copyable_v<T>
  {
    T value{};
    read(&value, sizeof(value));
    return value;
  }
  // The count is checked against what is left before anything is allocated, so a corrupt length
  // cannot ask for gigabytes.
  template <typename T>
  void array(std::vector<T> &out)
    requires std::is_trivially_copyable_v<T>
  {
    const std::uint32_t count = u32();
    if (!m_ok || count > remaining() / sizeof(T)) {
      m_ok = false;
      out.clear();
      return;
    }
    out.resize(count);
    read(out.data(), static_cast<std::size_t>(count) * sizeof(T));
  }
  [[nodiscard]] std::string string() {
    const std::uint32_t size = u32();
    if (!m_ok || size > remaining()) {
      m_ok = false;
      return {};
    }
    std::string text(size, '\0');
    read(text.data(), size);
    return text;
  }

private:
  void read(void *data, std::size_t size) {
    if (!m_ok || size > remaining()) {
      m_ok = false;
      return;
    }
    std::memcpy(data, m_bytes.data() + m_cursor, size);
    m_cursor += size;
  }

  std::span<const std::byte> m_bytes;
  std::size_t m_cursor{0};
  bool m_ok{true};
};

} // namespace sonnet::assets::detail
