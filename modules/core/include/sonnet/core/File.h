#pragma once

#include <sonnet/core/Error.h>

#include <cstddef>
#include <filesystem>
#include <vector>

namespace sonnet::core {

// Whole-file read. Missing or unreadable files are an Io error, not an exception: callers such
// as the editor show the message and continue.
[[nodiscard]] Result<std::vector<std::byte>> readFile(const std::filesystem::path &path);

} // namespace sonnet::core
