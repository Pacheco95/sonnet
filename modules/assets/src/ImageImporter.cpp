#include <sonnet/assets/Importers.h>

#include <sonnet/core/Log.h>
#include <sonnet/core/Profile.h>

// stb_image is header-only; this is its one implementation in the engine.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_FAILURE_USERMSG
#include <stb_image.h>

#include <glm/gtc/packing.hpp>

#include <cstring>
#include <format>
#include <limits>
#include <memory>

namespace sonnet::assets {

namespace {

struct StbFree {
  void operator()(void *pixels) const noexcept {
    stbi_image_free(pixels);
  }
};

int clampedLength(std::span<const std::byte> bytes) {
  return static_cast<int>(
      std::min<std::size_t>(bytes.size(), static_cast<std::size_t>(std::numeric_limits<int>::max())));
}

} // namespace

core::Result<renderer::TextureData> importImage(std::span<const std::byte> bytes, const TextureSettings &settings) {
  SONNET_ZONE();
  int width = 0;
  int height = 0;
  int channels = 0;
  const std::unique_ptr<stbi_uc, StbFree> pixels{stbi_load_from_memory(reinterpret_cast<const stbi_uc *>(bytes.data()),
                                                                       clampedLength(bytes), &width, &height, &channels,
                                                                       STBI_rgb_alpha)};
  if (!pixels) {
    return std::unexpected(
        core::Error{std::format("image decoding failed: {}", stbi_failure_reason()), core::ErrorCategory::Io});
  }
  renderer::TextureData texture{.size = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)},
                                .format = settings.srgb ? rhi::Format::R8G8B8A8Srgb : rhi::Format::R8G8B8A8Unorm,
                                .mipLevels = 1,
                                .cube = false,
                                .data = {}};
  texture.data.resize(static_cast<std::size_t>(texture.expectedSize()));
  std::memcpy(texture.data.data(), pixels.get(), texture.data.size());
  if (settings.mipmaps) {
    renderer::generateMipChain(texture);
  }
  return texture;
}

core::Result<renderer::TextureData> importHdr(std::span<const std::byte> bytes) {
  SONNET_ZONE();
  int width = 0;
  int height = 0;
  int channels = 0;
  const std::unique_ptr<float, StbFree> pixels{stbi_loadf_from_memory(
      reinterpret_cast<const stbi_uc *>(bytes.data()), clampedLength(bytes), &width, &height, &channels, 4)};
  if (!pixels) {
    return std::unexpected(
        core::Error{std::format("HDR decoding failed: {}", stbi_failure_reason()), core::ErrorCategory::Io});
  }
  renderer::TextureData texture{.size = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)},
                                .format = rhi::Format::R16G16B16A16Sfloat,
                                .mipLevels = 1,
                                .cube = false,
                                .data = {}};
  texture.data.resize(static_cast<std::size_t>(texture.expectedSize()));
  auto *halves = reinterpret_cast<std::uint16_t *>(texture.data.data());
  const std::size_t count = std::size_t{texture.size.x} * texture.size.y * 4;
  for (std::size_t i = 0; i < count; ++i) {
    // Values beyond half precision clamp rather than becoming infinities in the cube.
    halves[i] = glm::packHalf1x16(std::min(pixels.get()[i], 65000.0f));
  }
  return texture;
}

} // namespace sonnet::assets
