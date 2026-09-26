#pragma once

#include <sonnet/core/Error.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// The stream wraps SDL's; declaring it here keeps SDL's headers out, as Window.h does.
struct SDL_IOStream;

namespace sonnet::platform {

// A seekable, read-only stream over the game's content (docs/platform.md, "Paths"), opened by
// Platform::openContent. On Android a relative path is an asset inside the APK, read in place
// because the APK stores it uncompressed; everywhere else it is an ordinary file. One stream has
// one position, so it is not shared between threads.
class ContentStream {
public:
  ContentStream(SDL_IOStream *stream, std::string path) noexcept;
  ~ContentStream();
  ContentStream(ContentStream &&other) noexcept;
  ContentStream &operator=(ContentStream &&other) noexcept;
  ContentStream(const ContentStream &) = delete;
  ContentStream &operator=(const ContentStream &) = delete;

  // Up to buffer.size() bytes from the current position; fewer only at the end of the content.
  [[nodiscard]] core::Result<std::size_t> read(std::span<std::byte> buffer);
  // Exactly buffer.size() bytes, or an Io error when the content ends first.
  [[nodiscard]] core::Result<void> readExactly(std::span<std::byte> buffer);
  // Moves the position to an offset from the start. Past the end is an error.
  [[nodiscard]] core::Result<void> seek(std::uint64_t offset);
  [[nodiscard]] std::uint64_t tell() const;
  [[nodiscard]] std::uint64_t size() const;
  // All of it, from the start, whatever the position was.
  [[nodiscard]] core::Result<std::vector<std::byte>> readAll();

  // The path as it was given to openContent, for messages.
  [[nodiscard]] const std::string &path() const noexcept {
    return m_path;
  }

private:
  SDL_IOStream *m_stream{nullptr};
  std::string m_path;
};

} // namespace sonnet::platform
