#include <sonnet/core/Uuid.h>

#include <charconv>
#include <random>

namespace sonnet::core {

namespace {

std::mt19937_64 &randomEngine() {
  thread_local std::mt19937_64 s_engine{[] {
    std::random_device device;
    std::seed_seq seed{device(), device(), device(), device()};
    return std::mt19937_64{seed};
  }()};
  return s_engine;
}

constexpr std::string_view HexDigits = "0123456789abcdef";

constexpr int hexValue(char c) noexcept {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

} // namespace

Uuid Uuid::generate() {
  std::uniform_int_distribution<std::uint64_t> distribution;
  std::uint64_t words[2] = {distribution(randomEngine()), distribution(randomEngine())};
  Bytes bytes;
  for (std::size_t i = 0; i < 8; ++i) {
    bytes[i] = static_cast<std::uint8_t>(words[0] >> (8 * i));
    bytes[i + 8] = static_cast<std::uint8_t>(words[1] >> (8 * i));
  }
  bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0F) | 0x40); // version 4
  bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3F) | 0x80); // RFC 9562 variant
  return Uuid{bytes};
}

std::optional<Uuid> Uuid::parse(std::string_view text) noexcept {
  if (text.size() == 38 && text.front() == '{' && text.back() == '}') {
    text = text.substr(1, 36);
  }
  if (text.size() != 36) {
    return std::nullopt;
  }
  Bytes bytes{};
  std::size_t byteIndex = 0;
  for (std::size_t i = 0; i < 36;) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (text[i] != '-') {
        return std::nullopt;
      }
      ++i;
      continue;
    }
    const int high = hexValue(text[i]);
    const int low = hexValue(text[i + 1]);
    if (high < 0 || low < 0) {
      return std::nullopt;
    }
    bytes[byteIndex++] = static_cast<std::uint8_t>((high << 4) | low);
    i += 2;
  }
  return Uuid{bytes};
}

std::string Uuid::toString() const {
  std::string out;
  out.reserve(36);
  for (std::size_t i = 0; i < 16; ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10) {
      out.push_back('-');
    }
    out.push_back(HexDigits[m_bytes[i] >> 4]);
    out.push_back(HexDigits[m_bytes[i] & 0x0F]);
  }
  return out;
}

} // namespace sonnet::core
