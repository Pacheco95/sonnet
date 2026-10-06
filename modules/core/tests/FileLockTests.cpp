#include <sonnet/core/FileLock.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <optional>
#include <utility>

using sonnet::core::FileLock;

namespace {

std::filesystem::path scratchLock(const char *name) {
  const std::filesystem::path directory = std::filesystem::temp_directory_path() / "sonnet_core_lock_tests";
  std::filesystem::create_directories(directory);
  const std::filesystem::path path = directory / name;
  std::filesystem::remove(path);
  return path;
}

} // namespace

TEST_CASE("a lock creates its file and a second handle cannot take it", "[core][filelock]") {
  const std::filesystem::path path = scratchLock("conflict.lock");
  auto first = FileLock::tryLock(path);
  REQUIRE(first.has_value());
  REQUIRE(first->has_value());
  REQUIRE((*first)->held());
  REQUIRE(std::filesystem::exists(path));

  // Held by another handle of this process, the way a second process would be.
  const auto second = FileLock::tryLock(path);
  REQUIRE(second.has_value());
  REQUIRE(!second->has_value());
  std::filesystem::remove(path);
}

TEST_CASE("releasing a lock, explicitly or by closing it, lets the next handle in", "[core][filelock]") {
  const std::filesystem::path path = scratchLock("release.lock");

  SECTION("release") {
    auto first = FileLock::tryLock(path);
    REQUIRE(first.has_value());
    REQUIRE(first->has_value());
    (*first)->release();
    REQUIRE(!(*first)->held());
    (*first)->release(); // harmless twice
    REQUIRE(FileLock::tryLock(path).value().has_value());
  }
  SECTION("closing without deleting the file, what a killed process does") {
    {
      auto first = FileLock::tryLock(path);
      REQUIRE(first.value().has_value());
      REQUIRE(!FileLock::tryLock(path).value().has_value());
    }
    REQUIRE(std::filesystem::exists(path));
    REQUIRE(FileLock::tryLock(path).value().has_value());
  }
  std::filesystem::remove(path);
}

TEST_CASE("a lock moves with its holder", "[core][filelock]") {
  const std::filesystem::path path = scratchLock("move.lock");
  FileLock outer;
  REQUIRE(!outer.held());
  {
    auto inner = FileLock::tryLock(path);
    REQUIRE(inner.value().has_value());
    outer = std::move(**inner);
    REQUIRE(!(*inner)->held());
  }
  REQUIRE(outer.held());
  REQUIRE(!FileLock::tryLock(path).value().has_value());
  FileLock moved{std::move(outer)};
  REQUIRE(moved.held());
  REQUIRE(!outer.held());
  REQUIRE(!FileLock::tryLock(path).value().has_value());
  moved.release();
  REQUIRE(FileLock::tryLock(path).value().has_value());
  std::filesystem::remove(path);
}

TEST_CASE("a missing file is an error when the lock may not create it", "[core][filelock]") {
  const std::filesystem::path path = scratchLock("missing.lock");
  const auto absent = FileLock::tryLock(path, false);
  REQUIRE(!absent.has_value());
  REQUIRE(!std::filesystem::exists(path));

  REQUIRE(FileLock::tryLock(path).value().has_value()); // created, then released at once
  REQUIRE(FileLock::tryLock(path, false).value().has_value());
  std::filesystem::remove(path);
}
