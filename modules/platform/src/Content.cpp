#include <sonnet/platform/Content.h>
#include <sonnet/platform/Platform.h>

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>

#include <filesystem>
#include <format>
#include <string_view>
#include <utility>

namespace sonnet::platform {

namespace {

[[nodiscard]] core::Error contentError(std::string_view path, std::string_view what) {
  return core::Error{std::format("{}: {}", path, what), core::ErrorCategory::Io};
}

// SDL takes UTF-8 on every platform, which a path's native string is not on Windows.
[[nodiscard]] std::string utf8(const std::filesystem::path &path) {
  const std::u8string text = path.generic_u8string();
  return {reinterpret_cast<const char *>(text.data()), text.size()};
}

} // namespace

ContentStream::ContentStream(SDL_IOStream *stream, std::string path) noexcept
    : m_stream(stream), m_path(std::move(path)) {
}

ContentStream::~ContentStream() {
  if (m_stream != nullptr) {
    SDL_CloseIO(m_stream);
  }
}

ContentStream::ContentStream(ContentStream &&other) noexcept
    : m_stream(std::exchange(other.m_stream, nullptr)), m_path(std::move(other.m_path)) {
}

ContentStream &ContentStream::operator=(ContentStream &&other) noexcept {
  if (this != &other) {
    if (m_stream != nullptr) {
      SDL_CloseIO(m_stream);
    }
    m_stream = std::exchange(other.m_stream, nullptr);
    m_path = std::move(other.m_path);
  }
  return *this;
}

core::Result<std::size_t> ContentStream::read(std::span<std::byte> buffer) {
  std::size_t total = 0;
  // SDL may return less than asked before the end, so the read continues until it reports the
  // end or an error.
  while (total < buffer.size()) {
    const std::size_t count = SDL_ReadIO(m_stream, buffer.data() + total, buffer.size() - total);
    if (count == 0) {
      if (SDL_GetIOStatus(m_stream) != SDL_IO_STATUS_EOF) {
        return std::unexpected(contentError(m_path, std::format("read failed: {}", SDL_GetError())));
      }
      break;
    }
    total += count;
  }
  return total;
}

core::Result<void> ContentStream::readExactly(std::span<std::byte> buffer) {
  const auto count = read(buffer);
  if (!count) {
    return std::unexpected(count.error());
  }
  if (*count != buffer.size()) {
    return std::unexpected(contentError(
        m_path, std::format("ends {} bytes into a read of {} at offset {}", *count, buffer.size(), tell() - *count)));
  }
  return {};
}

core::Result<void> ContentStream::seek(std::uint64_t offset) {
  if (offset > size()) {
    return std::unexpected(contentError(m_path, std::format("cannot seek to {} past the end at {}", offset, size())));
  }
  if (SDL_SeekIO(m_stream, static_cast<Sint64>(offset), SDL_IO_SEEK_SET) < 0) {
    return std::unexpected(contentError(m_path, std::format("seek to {} failed: {}", offset, SDL_GetError())));
  }
  return {};
}

std::uint64_t ContentStream::tell() const {
  const Sint64 position = SDL_TellIO(m_stream);
  return position < 0 ? 0 : static_cast<std::uint64_t>(position);
}

std::uint64_t ContentStream::size() const {
  const Sint64 size = SDL_GetIOSize(m_stream);
  return size < 0 ? 0 : static_cast<std::uint64_t>(size);
}

core::Result<std::vector<std::byte>> ContentStream::readAll() {
  std::vector<std::byte> bytes(static_cast<std::size_t>(size()));
  if (auto sought = seek(0); !sought) {
    return std::unexpected(sought.error());
  }
  if (auto read = readExactly(bytes); !read) {
    return std::unexpected(read.error());
  }
  return bytes;
}

core::Result<ContentStream> Platform::openContent(const std::filesystem::path &path) {
#if defined(__ANDROID__)
  // SDL opens an absolute path as a file, and looks a relative one up in the app's internal
  // storage and then in the APK's assets/, which is where the content root is.
  const std::string resolved = utf8(path);
#else
  std::string resolved = utf8(path);
  if (path.is_relative()) {
    // Beside the binary, as basePath says; SDL keeps the string for the life of the process.
    const char *base = SDL_GetBasePath();
    if (base == nullptr) {
      return std::unexpected(contentError(resolved, std::format("no content root: {}", SDL_GetError())));
    }
    resolved = utf8(std::filesystem::path{reinterpret_cast<const char8_t *>(base)} / path);
  }
#endif
  SDL_IOStream *stream = SDL_IOFromFile(resolved.c_str(), "rb");
  if (stream == nullptr) {
    return std::unexpected(contentError(resolved, std::format("cannot open: {}", SDL_GetError())));
  }
  return ContentStream{stream, resolved};
}

} // namespace sonnet::platform
