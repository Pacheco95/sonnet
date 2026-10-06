#include <sonnet/core/FileLock.h>

#include <format>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace sonnet::core {

FileLock::FileLock(FileLock &&other) noexcept : m_handle(std::exchange(other.m_handle, Invalid)) {
}

FileLock &FileLock::operator=(FileLock &&other) noexcept {
  if (this != &other) {
    release();
    m_handle = std::exchange(other.m_handle, Invalid);
  }
  return *this;
}

FileLock::~FileLock() {
  release();
}

#if defined(_WIN32)

Result<std::optional<FileLock>> FileLock::tryLock(const std::filesystem::path &path, bool create) {
  // Shared with everyone, deletion included: the holder removes the file after releasing, and a
  // process probing it opens it too. Only the byte range is exclusive.
  const HANDLE handle =
      ::CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, create ? OPEN_ALWAYS : OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return std::unexpected(Error{std::format("cannot open {} for locking (error {})", path.string(), ::GetLastError()),
                                 ErrorCategory::Io});
  }
  OVERLAPPED range{};
  if (::LockFileEx(handle, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &range) != 0) {
    return std::optional<FileLock>{FileLock{reinterpret_cast<std::intptr_t>(handle)}};
  }
  const DWORD failure = ::GetLastError();
  ::CloseHandle(handle);
  if (failure == ERROR_LOCK_VIOLATION || failure == ERROR_IO_PENDING) {
    return std::optional<FileLock>{};
  }
  return std::unexpected(Error{std::format("cannot lock {} (error {})", path.string(), failure), ErrorCategory::Io});
}

void FileLock::release() noexcept {
  if (m_handle != Invalid) {
    ::CloseHandle(reinterpret_cast<HANDLE>(m_handle)); // closing drops the lock
    m_handle = Invalid;
  }
}

#else

Result<std::optional<FileLock>> FileLock::tryLock(const std::filesystem::path &path, bool create) {
  int fd = -1;
  do {
    fd = ::open(path.c_str(), O_RDWR | O_CLOEXEC | (create ? O_CREAT : 0), 0600);
  } while (fd < 0 && errno == EINTR);
  if (fd < 0) {
    return std::unexpected(
        Error{std::format("cannot open {} for locking: {}", path.string(), std::strerror(errno)), ErrorCategory::Io});
  }
  int locked = -1;
  do {
    locked = ::flock(fd, LOCK_EX | LOCK_NB);
  } while (locked < 0 && errno == EINTR);
  if (locked == 0) {
    return std::optional<FileLock>{FileLock{fd}};
  }
  const int failure = errno;
  ::close(fd);
  if (failure == EWOULDBLOCK) {
    return std::optional<FileLock>{};
  }
  return std::unexpected(
      Error{std::format("cannot lock {}: {}", path.string(), std::strerror(failure)), ErrorCategory::Io});
}

void FileLock::release() noexcept {
  if (m_handle != Invalid) {
    ::close(static_cast<int>(m_handle)); // closing drops the lock
    m_handle = Invalid;
  }
}

#endif

} // namespace sonnet::core
