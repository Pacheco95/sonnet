#include <sonnet/core/File.h>

#include <format>
#include <fstream>
#include <ios>

namespace sonnet::core {

Result<std::vector<std::byte>> readFile(const std::filesystem::path &path) {
  std::ifstream stream{path, std::ios::binary | std::ios::ate};
  if (!stream) {
    return std::unexpected(Error{std::format("cannot open {}", path.string()), ErrorCategory::Io});
  }
  const std::streamsize size = stream.tellg();
  if (size < 0) {
    return std::unexpected(Error{std::format("cannot read the size of {}", path.string()), ErrorCategory::Io});
  }
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  stream.seekg(0);
  if (size > 0 && !stream.read(reinterpret_cast<char *>(bytes.data()), size)) {
    return std::unexpected(Error{std::format("cannot read {}", path.string()), ErrorCategory::Io});
  }
  return bytes;
}

Result<void> writeFile(const std::filesystem::path &path, std::span<const std::byte> bytes) {
  std::error_code error;
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
      return std::unexpected(
          Error{std::format("cannot create {}: {}", path.parent_path().string(), error.message()), ErrorCategory::Io});
    }
  }
  std::ofstream stream{path, std::ios::binary};
  if (!stream) {
    return std::unexpected(Error{std::format("cannot open {} for writing", path.string()), ErrorCategory::Io});
  }
  if (!bytes.empty() &&
      !stream.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
    return std::unexpected(Error{std::format("cannot write {}", path.string()), ErrorCategory::Io});
  }
  return {};
}

Result<void> writeFile(const std::filesystem::path &path, std::string_view text) {
  return writeFile(path, std::as_bytes(std::span{text}));
}

} // namespace sonnet::core
