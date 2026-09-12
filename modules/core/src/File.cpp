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

} // namespace sonnet::core
