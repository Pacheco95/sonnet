// The helpers in tests/support/TestSupport.h, which every suite compiles against.
#include <TestSupport.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

TEST_CASE("removeScratch removes a tree, and a file still open fails it without throwing", "[core][test-support]") {
  const std::filesystem::path root = std::filesystem::temp_directory_path() / "sonnet_remove_scratch";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "nested");
  std::ofstream{root / "nested" / "closed.txt"} << "closed";
  {
    std::ofstream held{root / "nested" / "held.txt"};
    held << "held";
    held.flush();
#if defined(_WIN32)
    // Windows will not delete a file that is open without FILE_SHARE_DELETE, as std::ofstream opens
    // it: the throwing overload fails the test, the helper only reports it (issue #427).
    REQUIRE_THROWS_AS(std::filesystem::remove_all(root), std::filesystem::filesystem_error);
    bool removed = true;
    REQUIRE_NOTHROW(removed = sonnet::test::removeScratch(root));
    REQUIRE_FALSE(removed);
    REQUIRE(std::filesystem::exists(root / "nested" / "held.txt"));
#endif
  }
  REQUIRE(sonnet::test::removeScratch(root));
  REQUIRE_FALSE(std::filesystem::exists(root));
  REQUIRE(sonnet::test::removeScratch(root)); // nothing to remove is not a failure
}
