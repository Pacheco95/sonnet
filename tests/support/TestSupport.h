#pragma once

// Helpers shared by every test executable (tests/support is on each suite's include path).

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <system_error>
#include <thread>

namespace sonnet::test {

// Removes a test's scratch directory when the test is done with it, and never throws, so cleanup
// cannot fail a test that passed (issue #427). Windows refuses to delete a file that is still open,
// whether by a worker that has not quite let go or by a virus scanner on the runner, so a failure
// is retried a few times over about a second before it gives up with a warning on stderr. What is
// left lives in the process's own temporary directory (TestSupport.cpp), removed again on exit.
// Returns whether the directory is gone. Setup code that needs an empty directory should keep the
// throwing std::filesystem::remove_all, so a leftover is reported rather than read.
inline bool removeScratch(const std::filesystem::path &path) noexcept {
  std::error_code error;
  for (int attempt = 0; attempt < 5; ++attempt) {
    if (attempt > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds{50 << attempt}); // 100, 200, 400, 800 ms
    }
    error.clear();
    std::filesystem::remove_all(path, error);
    if (!error) {
      return true;
    }
  }
  // UTF-8, since path::string() throws for a name the narrow encoding cannot hold.
  const std::u8string name = path.u8string();
  std::fprintf(stderr, "warning: could not remove %s: %s\n", reinterpret_cast<const char *>(name.c_str()),
               error.message().c_str());
  return false;
}

} // namespace sonnet::test
