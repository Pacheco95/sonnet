#include <sonnet/renderer/Texture.h>

#include <sonnet/core/Assert.h>

#include <algorithm>
#include <cmath>

namespace sonnet::renderer {

namespace {

float srgbToLinear(std::uint8_t value) {
  const float c = static_cast<float>(value) / 255.0f;
  return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

std::uint8_t linearToSrgb(float value) {
  const float c = value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
  return static_cast<std::uint8_t>(std::lround(std::clamp(c, 0.0f, 1.0f) * 255.0f));
}

} // namespace

std::span<const std::byte> TextureData::level(std::uint32_t mipLevel, std::uint32_t layer) const {
  if (mipLevel >= mipLevels || layer >= layers()) {
    return {};
  }
  std::uint64_t offset = 0;
  for (std::uint32_t l = 0; l < mipLevel; ++l) {
    offset += rhi::levelByteSize(format, rhi::mipSize(size, l)) * layers();
  }
  const std::uint64_t bytes = rhi::levelByteSize(format, rhi::mipSize(size, mipLevel));
  offset += bytes * layer;
  if (offset + bytes > data.size()) {
    return {};
  }
  return std::span{data}.subspan(static_cast<std::size_t>(offset), static_cast<std::size_t>(bytes));
}

std::uint64_t TextureData::expectedSize() const noexcept {
  std::uint64_t bytes = 0;
  for (std::uint32_t l = 0; l < mipLevels; ++l) {
    bytes += rhi::levelByteSize(format, rhi::mipSize(size, l));
  }
  return bytes * layers();
}

TextureData solidTexture(glm::u8vec4 color, rhi::Format format) {
  TextureData texture{.size = {1, 1}, .format = format, .mipLevels = 1, .cube = false, .data = {}};
  texture.data = {std::byte{color.r}, std::byte{color.g}, std::byte{color.b}, std::byte{color.a}};
  return texture;
}

void generateMipChain(TextureData &texture) {
  SONNET_ASSERT(texture.format == rhi::Format::R8G8B8A8Unorm || texture.format == rhi::Format::R8G8B8A8Srgb,
                "mip generation takes RGBA8 data");
  SONNET_ASSERT(texture.mipLevels == 1 && !texture.cube, "mip generation takes a single-level 2D image");
  const bool srgb = texture.format == rhi::Format::R8G8B8A8Srgb;
  const std::uint32_t levels = rhi::fullMipCount(texture.size);
  std::vector<std::byte> chain;
  chain.reserve(static_cast<std::size_t>(texture.expectedSize() * 4 / 3 + 16));
  chain.insert(chain.end(), texture.data.begin(), texture.data.end());
  glm::uvec2 previousSize = texture.size;
  std::size_t previousOffset = 0;
  for (std::uint32_t level = 1; level < levels; ++level) {
    const glm::uvec2 levelSize = rhi::mipSize(texture.size, level);
    const std::size_t levelOffset = chain.size();
    chain.resize(levelOffset + static_cast<std::size_t>(rhi::levelByteSize(texture.format, levelSize)));
    for (std::uint32_t y = 0; y < levelSize.y; ++y) {
      for (std::uint32_t x = 0; x < levelSize.x; ++x) {
        // A 2x2 box; edges of odd sizes are clamped so the last row and column are counted twice.
        float sum[4]{};
        for (std::uint32_t dy = 0; dy < 2; ++dy) {
          for (std::uint32_t dx = 0; dx < 2; ++dx) {
            const std::uint32_t sx = std::min(x * 2 + dx, previousSize.x - 1);
            const std::uint32_t sy = std::min(y * 2 + dy, previousSize.y - 1);
            const std::size_t source = previousOffset + (static_cast<std::size_t>(sy) * previousSize.x + sx) * 4;
            for (std::size_t c = 0; c < 4; ++c) {
              const auto value = std::to_integer<std::uint8_t>(chain[source + c]);
              sum[c] += srgb && c < 3 ? srgbToLinear(value) : static_cast<float>(value) / 255.0f;
            }
          }
        }
        const std::size_t destination = levelOffset + (static_cast<std::size_t>(y) * levelSize.x + x) * 4;
        for (std::size_t c = 0; c < 4; ++c) {
          const float average = sum[c] * 0.25f;
          chain[destination + c] = std::byte{srgb && c < 3 ? linearToSrgb(average)
                                                           : static_cast<std::uint8_t>(std::lround(average * 255.0f))};
        }
      }
    }
    previousSize = levelSize;
    previousOffset = levelOffset;
  }
  texture.data = std::move(chain);
  texture.mipLevels = levels;
}

} // namespace sonnet::renderer
