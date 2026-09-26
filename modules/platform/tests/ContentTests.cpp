#include <sonnet/platform/Content.h>
#include <sonnet/platform/Platform.h>

#include <sonnet/core/Error.h>

#include <catch2/catch_test_macros.hpp>

#include <SDL3/SDL_filesystem.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

using sonnet::platform::ContentStream;
using sonnet::platform::Platform;

namespace {

// 256 bytes counting up, so a byte's value is its offset.
std::vector<std::byte> ramp() {
  std::vector<std::byte> bytes(256);
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    bytes[i] = static_cast<std::byte>(i);
  }
  return bytes;
}

// A file in the content root, which is basePath() on desktop: beside the test binary.
class ContentFile {
public:
  explicit ContentFile(std::string name) : m_name(std::move(name)) {
    const std::vector<std::byte> bytes = ramp();
    std::ofstream out{absolute(), std::ios::binary | std::ios::trunc};
    out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  ~ContentFile() {
    std::error_code error;
    std::filesystem::remove(absolute(), error);
  }
  ContentFile(const ContentFile &) = delete;
  ContentFile &operator=(const ContentFile &) = delete;

  [[nodiscard]] const std::string &name() const noexcept {
    return m_name;
  }
  [[nodiscard]] std::filesystem::path absolute() const {
    return std::filesystem::path{SDL_GetBasePath()} / m_name;
  }

private:
  std::string m_name;
};

} // namespace

TEST_CASE("openContent resolves a relative path against the base path", "[platform][content]") {
  const ContentFile file{"content_tests_relative.bin"};
  auto stream = Platform::openContent(file.name());
  REQUIRE(stream);
  REQUIRE(stream->size() == 256);
  REQUIRE(stream->tell() == 0);
  const auto bytes = stream->readAll();
  REQUIRE(bytes);
  REQUIRE(*bytes == ramp());
}

TEST_CASE("openContent opens an absolute path as an ordinary file", "[platform][content]") {
  const std::filesystem::path path = std::filesystem::temp_directory_path() / "sonnet_content_tests_absolute.bin";
  {
    const std::vector<std::byte> bytes = ramp();
    std::ofstream out{path, std::ios::binary | std::ios::trunc};
    out.write(reinterpret_cast<const char *>(bytes.data()), 100);
  }
  REQUIRE(path.is_absolute());
  auto stream = Platform::openContent(path);
  REQUIRE(stream);
  REQUIRE(stream->size() == 100);
  std::filesystem::remove(path);
}

TEST_CASE("openContent reports a missing file as an Io error naming it", "[platform][content]") {
  const auto relative = Platform::openContent("content_tests_missing.bin");
  REQUIRE_FALSE(relative);
  REQUIRE(relative.error().category == sonnet::core::ErrorCategory::Io);
  REQUIRE(relative.error().message.find("content_tests_missing.bin") != std::string::npos);

  const auto absolute = Platform::openContent(std::filesystem::temp_directory_path() / "sonnet_no_such_file.bin");
  REQUIRE_FALSE(absolute);
  REQUIRE(absolute.error().message.find("sonnet_no_such_file.bin") != std::string::npos);
}

TEST_CASE("a content stream seeks, tells and reads at an offset", "[platform][content]") {
  const ContentFile file{"content_tests_seek.bin"};
  auto stream = Platform::openContent(file.name());
  REQUIRE(stream);

  REQUIRE(stream->seek(200));
  REQUIRE(stream->tell() == 200);
  std::array<std::byte, 8> eight{};
  REQUIRE(stream->readExactly(eight));
  REQUIRE(eight[0] == std::byte{200});
  REQUIRE(eight[7] == std::byte{207});
  REQUIRE(stream->tell() == 208);

  // Back to an earlier offset: the position is absolute, not relative to the last read.
  REQUIRE(stream->seek(16));
  REQUIRE(stream->readExactly(eight));
  REQUIRE(eight[0] == std::byte{16});

  // A read that crosses the end returns what there is; an exact one reports the short read.
  REQUIRE(stream->seek(252));
  const auto count = stream->read(eight);
  REQUIRE(count);
  REQUIRE(*count == 4);
  REQUIRE(eight[3] == std::byte{255});
  REQUIRE(stream->seek(252));
  const auto exact = stream->readExactly(eight);
  REQUIRE_FALSE(exact);
  REQUIRE(exact.error().category == sonnet::core::ErrorCategory::Io);

  // The end itself is a position; past it is not.
  REQUIRE(stream->seek(256));
  REQUIRE_FALSE(stream->seek(257));

  // readAll starts from the beginning whatever the position.
  REQUIRE(stream->seek(100));
  const auto all = stream->readAll();
  REQUIRE(all);
  REQUIRE(all->size() == 256);
}

TEST_CASE("a moved content stream keeps its file", "[platform][content]") {
  const ContentFile file{"content_tests_move.bin"};
  auto opened = Platform::openContent(file.name());
  REQUIRE(opened);
  ContentStream stream = std::move(*opened);
  REQUIRE(stream.size() == 256);
  REQUIRE(stream.path().ends_with("content_tests_move.bin"));
}
