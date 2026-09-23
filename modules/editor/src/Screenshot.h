#pragma once

#include <sonnet/core/Error.h>
#include <sonnet/core/Math.h>
#include <sonnet/rhi/Types.h>

#include <cstddef>
#include <filesystem>
#include <span>

namespace sonnet::editor {

// Tightly packed 8-bit RGBA or BGRA pixels, as copyImageToBuffer leaves them, written as an
// opaque PNG: the scene's alpha is not coverage and would punch holes in the image.
[[nodiscard]] core::Result<void> writeScreenshot(const std::filesystem::path &file, glm::uvec2 size, rhi::Format format,
                                                 std::span<const std::byte> pixels);

} // namespace sonnet::editor
