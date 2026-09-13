#include <sonnet/core/Uuid.h>

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <unordered_set>

TEST_CASE("default Uuid is nil", "[core][uuid]") {
  constexpr sonnet::core::Uuid nil;
  STATIC_REQUIRE(nil.isNil());
  REQUIRE(nil.toString() == "00000000-0000-0000-0000-000000000000");
}

TEST_CASE("generated Uuids are version 4 and unique", "[core][uuid]") {
  std::unordered_set<sonnet::core::Uuid> seen;
  for (int i = 0; i < 1000; ++i) {
    const auto uuid = sonnet::core::Uuid::generate();
    REQUIRE(!uuid.isNil());
    REQUIRE((uuid.bytes()[6] & 0xF0) == 0x40);
    REQUIRE((uuid.bytes()[8] & 0xC0) == 0x80);
    REQUIRE(seen.insert(uuid).second);
  }
}

TEST_CASE("Uuid round-trips through its string form", "[core][uuid]") {
  const auto uuid = sonnet::core::Uuid::generate();
  const auto text = uuid.toString();
  REQUIRE(text.size() == 36);
  REQUIRE(sonnet::core::Uuid::parse(text) == uuid);
}

TEST_CASE("Uuid parse accepts case and braces and rejects garbage", "[core][uuid]") {
  const auto lower = sonnet::core::Uuid::parse("123e4567-e89b-12d3-a456-426614174000");
  REQUIRE(lower.has_value());
  REQUIRE(lower->bytes()[0] == 0x12);
  REQUIRE(lower->bytes()[15] == 0x00);
  REQUIRE(sonnet::core::Uuid::parse("123E4567-E89B-12D3-A456-426614174000") == lower);
  REQUIRE(sonnet::core::Uuid::parse("{123e4567-e89b-12d3-a456-426614174000}") == lower);
  REQUIRE(lower->toString() == "123e4567-e89b-12d3-a456-426614174000");

  REQUIRE(!sonnet::core::Uuid::parse("").has_value());
  REQUIRE(!sonnet::core::Uuid::parse("123e4567e89b12d3a456426614174000").has_value());
  REQUIRE(!sonnet::core::Uuid::parse("123e4567-e89b-12d3-a456-42661417400g").has_value());
  REQUIRE(!sonnet::core::Uuid::parse("123e4567+e89b-12d3-a456-426614174000").has_value());
}

TEST_CASE("derived Uuids are stable for a parent and name and distinct otherwise", "[core][uuid]") {
  const auto parent = sonnet::core::Uuid::parse("123e4567-e89b-12d3-a456-426614174000").value();
  const auto mesh = sonnet::core::Uuid::derive(parent, "mesh/0");
  REQUIRE(!mesh.isNil());
  REQUIRE(mesh == sonnet::core::Uuid::derive(parent, "mesh/0"));
  REQUIRE(mesh != sonnet::core::Uuid::derive(parent, "mesh/1"));
  REQUIRE(mesh != sonnet::core::Uuid::derive(sonnet::core::Uuid::generate(), "mesh/0"));
  REQUIRE((mesh.bytes()[6] & 0xF0) == 0x80);
  REQUIRE((mesh.bytes()[8] & 0xC0) == 0x80);
  REQUIRE(sonnet::core::Uuid::parse(mesh.toString()) == mesh);
}
