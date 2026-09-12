#include <sonnet/core/Handle.h>

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <type_traits>

namespace {

struct BufferTag {};
struct ImageTag {};
using BufferHandle = sonnet::core::Handle<BufferTag>;
using ImageHandle = sonnet::core::Handle<ImageTag>;

} // namespace

TEST_CASE("default handle is invalid", "[core][handle]") {
  constexpr BufferHandle handle;
  STATIC_REQUIRE(!handle.isValid());
  STATIC_REQUIRE(!handle);
  STATIC_REQUIRE(handle.index == BufferHandle::InvalidIndex);
}

TEST_CASE("handles of different tags are distinct types", "[core][handle]") {
  STATIC_REQUIRE(!std::is_convertible_v<BufferHandle, ImageHandle>);
  STATIC_REQUIRE(!std::is_assignable_v<BufferHandle &, ImageHandle>);
}

TEST_CASE("handles compare by index and generation", "[core][handle]") {
  constexpr BufferHandle a{3, 1};
  constexpr BufferHandle sameSlotLaterGeneration{3, 2};
  constexpr BufferHandle otherSlot{4, 1};
  STATIC_REQUIRE(a == BufferHandle{3, 1});
  STATIC_REQUIRE(a != sameSlotLaterGeneration);
  STATIC_REQUIRE(a != otherSlot);
  STATIC_REQUIRE(a < sameSlotLaterGeneration);
}

TEST_CASE("packed form round-trips", "[core][handle]") {
  constexpr BufferHandle handle{0x12345678u, 0x9ABCDEF0u};
  STATIC_REQUIRE(handle.packed() == 0x9ABCDEF012345678ull);
  STATIC_REQUIRE(BufferHandle::fromPacked(handle.packed()) == handle);
}

TEST_CASE("handles are hashable", "[core][handle]") {
  const std::hash<BufferHandle> hash;
  REQUIRE(hash(BufferHandle{1, 1}) == hash(BufferHandle{1, 1}));
  REQUIRE(hash(BufferHandle{1, 1}) != hash(BufferHandle{1, 2}));
}
