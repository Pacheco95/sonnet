#include <sonnet/assets/Json.h>

#include <cstdint>

namespace sonnet::assets {

nlohmann::json parseJson(std::span<const std::byte> text) {
  const auto *begin = reinterpret_cast<const char *>(text.data());
  return nlohmann::json::parse(begin, begin + text.size(), nullptr, false);
}

nlohmann::json parseCbor(std::span<const std::byte> bytes) {
  const auto *begin = reinterpret_cast<const std::uint8_t *>(bytes.data());
  return nlohmann::json::from_cbor(begin, begin + bytes.size(), true, false);
}

} // namespace sonnet::assets
