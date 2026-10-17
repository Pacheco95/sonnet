#pragma once

#include <sonnet/assets/Asset.h>

#include <sonnet/core/Error.h>
#include <sonnet/core/Uuid.h>
#include <sonnet/renderer/Texture.h>

#include <cstdint>
#include <filesystem>

namespace sonnet::assets {

// The longest side of a thumbnail, in pixels (docs/assets.md, "Thumbnails").
inline constexpr std::uint32_t ThumbnailSize = 128;

// What making one asset's thumbnail needs, copied out of the database so a worker can run it.
struct ThumbnailRequest {
  core::Uuid uuid;
  AssetType type{AssetType::Texture}; // Texture or Environment
  std::filesystem::path source;
  std::filesystem::path sidecar;
  std::filesystem::path cache; // the cache directory
  std::filesystem::file_time_type sourceTime;
  TextureSettings settings;
};

// The cache entry of an asset's thumbnail, `<cache>/<uuid>.thumb.ktx2`.
[[nodiscard]] std::filesystem::path thumbnailFile(const std::filesystem::path &cache, const core::Uuid &uuid);

// A picture of a texture or an environment, one RGBA8 level of at most ThumbnailSize on its longest
// side, its bytes as ImGui shows them (sRGB-encoded, so the format is Unorm). Read from the cache
// entry when it is no older than the source and its sidecar, and made from the source and written
// there otherwise. Pure: touches nothing but the files, so it runs on any thread.
[[nodiscard]] core::Result<renderer::TextureData> makeThumbnail(const ThumbnailRequest &request);

} // namespace sonnet::assets
