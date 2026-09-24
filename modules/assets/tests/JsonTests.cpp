#include <sonnet/assets/Json.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

using namespace sonnet;
using namespace sonnet::assets;

namespace {

[[nodiscard]] std::vector<std::byte> bytesOf(std::string_view text) {
  const auto *data = reinterpret_cast<const std::byte *>(text.data());
  return {data, data + text.size()};
}

[[nodiscard]] std::vector<std::byte> bytesOf(const std::vector<std::uint8_t> &values) {
  const auto *data = reinterpret_cast<const std::byte *>(values.data());
  return {data, data + values.size()};
}

} // namespace

// These are what every file read goes through, so the engine never hands nlohmann std::byte,
// which libc++ from LLVM 19 cannot compile (the Json.h comment). What they must keep is the
// behaviour of the calls they replaced: a discarded value for bad input, never an exception.
TEST_CASE("JSON text parses from bytes, and bad or empty text is discarded", "[assets][json]") {
  const nlohmann::json document = parseJson(bytesOf(R"({"name": "crate", "size": [1, 2]})"));
  REQUIRE(document.is_object());
  REQUIRE(document["name"] == "crate");
  REQUIRE(document["size"][1] == 2);

  REQUIRE(parseJson(bytesOf(R"({"name": )")).is_discarded());
  REQUIRE(parseJson(bytesOf("{} trailing")).is_discarded());
  REQUIRE(parseJson(std::span<const std::byte>{}).is_discarded());
}

TEST_CASE("CBOR parses from bytes, strictly, and bad or empty input is discarded", "[assets][json]") {
  const nlohmann::json original = {{"uuid", "0f1e"}, {"offset", 4096}, {"flags", {true, false}}};
  std::vector<std::byte> cbor = bytesOf(nlohmann::json::to_cbor(original));
  REQUIRE(parseCbor(cbor) == original);

  cbor.push_back(std::byte{0});
  REQUIRE(parseCbor(cbor).is_discarded());
  cbor.resize(cbor.size() / 2);
  REQUIRE(parseCbor(cbor).is_discarded());
  REQUIRE(parseCbor(std::span<const std::byte>{}).is_discarded());
}
