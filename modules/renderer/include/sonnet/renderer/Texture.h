#pragma once

#include <sonnet/core/Handle.h>
#include <sonnet/core/Math.h>
#include <sonnet/rhi/Types.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sonnet::renderer {

struct TextureTag {};
using TextureHandle = core::Handle<TextureTag>;

// Texture data ready for upload: every level tightly packed, level-major with the six faces of
// a cube map inside each level, as KTX2 lays them out. `levels` is 0 for a full chain down to
// 1x1 when the data holds one.
struct TextureData {
  glm::uvec2 size{1, 1};
  rhi::Format format{rhi::Format::R8G8B8A8Srgb};
  std::uint32_t mipLevels{1};
  bool cube{false};
  std::vector<std::byte> data;

  [[nodiscard]] std::uint32_t layers() const noexcept {
    return cube ? 6u : 1u;
  }
  // The bytes of one level and layer; empty when the data is too short.
  [[nodiscard]] std::span<const std::byte> level(std::uint32_t mipLevel, std::uint32_t layer = 0) const;
  [[nodiscard]] std::uint64_t expectedSize() const noexcept;
};

// A one-colour RGBA8 texture, for defaults and tests.
[[nodiscard]] TextureData solidTexture(glm::u8vec4 color, rhi::Format format = rhi::Format::R8G8B8A8Unorm);
// Box-filtered mip chain for an RGBA8 image with one level; sRGB formats are averaged in
// linear space.
void generateMipChain(TextureData &texture);

} // namespace sonnet::renderer
