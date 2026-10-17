#include <sonnet/assets/Thumbnail.h>

#include <sonnet/assets/Importers.h>
#include <sonnet/core/File.h>
#include <sonnet/core/Log.h>
#include <sonnet/core/Profile.h>

#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>

namespace sonnet::assets {

namespace {

// The oldest time there is for a file that does not exist, so a missing sidecar or dependency never makes an entry
// stale. (A default-constructed time is a date in 2174 on libstdc++.)
constexpr auto Missing = std::filesystem::file_time_type::min();

std::filesystem::file_time_type modified(const std::filesystem::path &file) {
  std::error_code error;
  const auto time = std::filesystem::last_write_time(file, error);
  return error ? Missing : time;
}

bool fresh(const ThumbnailRequest &request, const std::filesystem::path &cached) {
  const auto cachedTime = modified(cached);
  return cachedTime != Missing && cachedTime >= request.sourceTime && cachedTime >= modified(request.sidecar) &&
         std::ranges::all_of(request.dependencies, [&](const auto &file) { return cachedTime >= modified(file); });
}

// The largest level of a texture with a side no longer than the thumbnail's, or the smallest level
// when none is.
renderer::TextureData fromTexture(const renderer::TextureData &texture) {
  std::uint32_t level = 0;
  while (level + 1 < texture.mipLevels && std::max(texture.size.x, texture.size.y) >> level > ThumbnailSize) {
    ++level;
  }
  const auto bytes = texture.level(level);
  return {.size = {std::max(texture.size.x >> level, 1u), std::max(texture.size.y >> level, 1u)},
          .format = rhi::Format::R8G8B8A8Unorm,
          .mipLevels = 1,
          .cube = false,
          .data = {bytes.begin(), bytes.end()}};
}

std::uint8_t encode(float linear) {
  const float mapped = linear / (1.0f + linear); // Reinhard
  const float srgb = mapped <= 0.0031308f ? 12.92f * mapped : 1.055f * std::pow(mapped, 1.0f / 2.4f) - 0.055f;
  return static_cast<std::uint8_t>(std::lround(std::clamp(srgb, 0.0f, 1.0f) * 255.0f));
}

// `index` of `count` steps along `length`, rounded down.
std::uint32_t scaled(std::uint32_t index, std::uint32_t length, std::uint32_t count) {
  return static_cast<std::uint32_t>(std::uint64_t{index} * length / count);
}

// A box filter over an equirectangular RGBA16F map, tone mapped to RGBA8.
renderer::TextureData fromEnvironment(const renderer::TextureData &map) {
  const std::uint32_t width = std::min(map.size.x, ThumbnailSize);
  const std::uint32_t height =
      std::max(1u, static_cast<std::uint32_t>(std::lround(static_cast<double>(width) * map.size.y / map.size.x)));
  renderer::TextureData thumbnail{
      .size = {width, height}, .format = rhi::Format::R8G8B8A8Unorm, .mipLevels = 1, .cube = false, .data = {}};
  thumbnail.data.resize(static_cast<std::size_t>(thumbnail.expectedSize()));
  const auto *halves = reinterpret_cast<const std::uint16_t *>(map.data.data());
  auto *out = reinterpret_cast<std::uint8_t *>(thumbnail.data.data());
  for (std::uint32_t y = 0; y < height; ++y) {
    const std::uint32_t y0 = scaled(y, map.size.y, height);
    const std::uint32_t y1 = std::max(y0 + 1, scaled(y + 1, map.size.y, height));
    for (std::uint32_t x = 0; x < width; ++x) {
      const std::uint32_t x0 = scaled(x, map.size.x, width);
      const std::uint32_t x1 = std::max(x0 + 1, scaled(x + 1, map.size.x, width));
      glm::vec3 sum{0.0f};
      for (std::uint32_t sy = y0; sy < y1; ++sy) {
        for (std::uint32_t sx = x0; sx < x1; ++sx) {
          const std::uint16_t *texel = halves + (std::size_t{sy} * map.size.x + sx) * 4;
          sum += glm::vec3{glm::unpackHalf1x16(texel[0]), glm::unpackHalf1x16(texel[1]), glm::unpackHalf1x16(texel[2])};
        }
      }
      sum /= static_cast<float>((y1 - y0) * (x1 - x0));
      std::uint8_t *pixel = out + (std::size_t{y} * width + x) * 4;
      pixel[0] = encode(sum.r);
      pixel[1] = encode(sum.g);
      pixel[2] = encode(sum.b);
      pixel[3] = 255;
    }
  }
  return thumbnail;
}

} // namespace

std::filesystem::path thumbnailFile(const std::filesystem::path &cache, const core::Uuid &uuid) {
  return cache / (uuid.toString() + ".thumb.ktx2");
}

bool isRendered(const ThumbnailRequest &request) noexcept {
  return request.prefab || request.type == AssetType::Material || request.type == AssetType::Mesh ||
         request.type == AssetType::Model;
}

std::optional<renderer::TextureData> cachedThumbnail(const ThumbnailRequest &request) {
  const std::filesystem::path cached = thumbnailFile(request.cache, request.uuid);
  if (!fresh(request, cached)) {
    return std::nullopt;
  }
  const auto bytes = core::readFile(cached);
  if (!bytes) {
    return std::nullopt;
  }
  // RGBA8 needs nothing of the device.
  auto data = readKtx2(*bytes, {});
  return data ? std::optional{std::move(*data)} : std::nullopt;
}

core::Result<void> storeThumbnail(const ThumbnailRequest &request, const renderer::TextureData &picture) {
  if (!request.store) {
    return {};
  }
  const auto ktx = cookKtx2(picture, false);
  if (!ktx) {
    return std::unexpected(ktx.error());
  }
  return core::writeFile(thumbnailFile(request.cache, request.uuid), *ktx);
}

core::Result<renderer::TextureData> makeThumbnail(const ThumbnailRequest &request) {
  SONNET_ZONE();
  if (isRendered(request)) {
    return std::unexpected(core::Error{"this thumbnail is rendered, not made from its file", core::ErrorCategory::Io});
  }
  if (auto cached = cachedThumbnail(request)) {
    return std::move(*cached);
  }
  const auto bytes = core::readFile(request.source);
  if (!bytes) {
    return std::unexpected(bytes.error());
  }
  renderer::TextureData thumbnail;
  if (request.type == AssetType::Environment) {
    const auto map = importHdr(*bytes);
    if (!map) {
      return std::unexpected(map.error());
    }
    thumbnail = fromEnvironment(*map);
  } else {
    const auto image = importImage(*bytes, {.srgb = request.settings.srgb, .mipmaps = true, .compress = false});
    if (!image) {
      return std::unexpected(image.error());
    }
    thumbnail = fromTexture(*image);
  }
  if (const auto stored = storeThumbnail(request, thumbnail); !stored) {
    return std::unexpected(stored.error());
  }
  return thumbnail;
}

} // namespace sonnet::assets
