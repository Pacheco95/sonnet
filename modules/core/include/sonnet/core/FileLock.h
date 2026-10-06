#pragma once

#include <sonnet/core/Error.h>

#include <cstdint>
#include <filesystem>
#include <optional>

namespace sonnet::core {

// An exclusive OS lock on a file, held for as long as the object lives. The lock belongs to the open
// file, so it dies with the process (a crash included) and nothing has to be cleaned up to tell
// whether the holder is still running: that is "the lock cannot be taken". `flock` on Linux and macOS,
// `LockFileEx` on Windows; both conflict between two handles of one process, which is how tests
// simulate a second process. The file is opened close-on-exec (not inheritable on Windows), so a
// helper process spawned later cannot keep the lock alive. The file is never deleted by the lock.
class FileLock {
public:
  FileLock() = default;
  FileLock(const FileLock &) = delete;
  FileLock &operator=(const FileLock &) = delete;
  FileLock(FileLock &&other) noexcept;
  FileLock &operator=(FileLock &&other) noexcept;
  ~FileLock();

  // Tries to lock `path` without waiting. `create` makes the file when it is missing; otherwise a
  // missing file is an Io error. The result is the lock, or no value when another handle holds it, or
  // an Io error when the file cannot be opened or the filesystem does not support locking.
  [[nodiscard]] static Result<std::optional<FileLock>> tryLock(const std::filesystem::path &path, bool create = true);

  // Unlocks and closes. Harmless on a lock that is not held.
  void release() noexcept;
  [[nodiscard]] bool held() const noexcept {
    return m_handle != Invalid;
  }

private:
  static constexpr std::intptr_t Invalid = -1;
  explicit FileLock(std::intptr_t handle) noexcept : m_handle(handle) {
  }

  std::intptr_t m_handle{Invalid};
};

} // namespace sonnet::core
