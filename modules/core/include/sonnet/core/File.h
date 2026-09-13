#pragma once

#include <sonnet/core/Error.h>

#include <cstddef>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace sonnet::core {

// Whole-file read. Missing or unreadable files are an Io error, not an exception: callers such
// as the editor show the message and continue.
[[nodiscard]] Result<std::vector<std::byte>> readFile(const std::filesystem::path &path);
// Whole-file write, creating the parent directories. Same error policy.
[[nodiscard]] Result<void> writeFile(const std::filesystem::path &path, std::span<const std::byte> bytes);
[[nodiscard]] Result<void> writeFile(const std::filesystem::path &path, std::string_view text);

} // namespace sonnet::core
