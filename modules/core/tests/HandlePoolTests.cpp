#include <sonnet/core/HandlePool.h>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

namespace {

struct ThingTag {};
struct Thing {
  std::string name;
  int value{0};
};
using Pool = sonnet::core::HandlePool<Thing, ThingTag>;

} // namespace

TEST_CASE("emplace returns a handle that resolves to the object", "[core][handle]") {
  Pool pool;
  const auto handle = pool.emplace("first", 7);
  REQUIRE(handle.isValid());
  REQUIRE(pool.contains(handle));
  REQUIRE(pool.get(handle).name == "first");
  REQUIRE(pool.find(handle)->value == 7);
  REQUIRE(pool.size() == 1);
}

TEST_CASE("a removed handle is stale and does not resolve to the slot's next occupant", "[core][handle]") {
  Pool pool;
  const auto first = pool.emplace("first");
  auto removed = pool.remove(first);
  REQUIRE(removed.has_value());
  REQUIRE(removed->name == "first");
  REQUIRE(!pool.contains(first));
  REQUIRE(pool.find(first) == nullptr);
  REQUIRE(pool.empty());

  const auto second = pool.emplace("second");
  REQUIRE(second.index == first.index);
  REQUIRE(second.generation != first.generation);
  REQUIRE(!pool.contains(first));
  REQUIRE(pool.get(second).name == "second");
  REQUIRE(!pool.remove(first).has_value());
}

TEST_CASE("invalid and out-of-range handles do not resolve", "[core][handle]") {
  Pool pool;
  REQUIRE(!pool.contains(Pool::HandleType{}));
  REQUIRE(!pool.contains(Pool::HandleType{42, 1}));
  REQUIRE(pool.find(Pool::HandleType{}) == nullptr);
}

TEST_CASE("pool holds move-only objects", "[core][handle]") {
  struct UniqueTag {};
  sonnet::core::HandlePool<std::unique_ptr<int>, UniqueTag> pool;
  const auto handle = pool.emplace(std::make_unique<int>(5));
  REQUIRE(*pool.get(handle) == 5);
  auto out = pool.remove(handle);
  REQUIRE(out.has_value());
  REQUIRE(**out == 5);
}

TEST_CASE("forEach visits live objects with their handles", "[core][handle]") {
  Pool pool;
  const auto a = pool.emplace("a");
  const auto b = pool.emplace("b");
  const auto c = pool.emplace("c");
  pool.remove(b);

  std::vector<Pool::HandleType> visited;
  pool.forEach([&](Pool::HandleType handle, Thing &) { visited.push_back(handle); });
  REQUIRE(visited == std::vector<Pool::HandleType>{a, c});
}
