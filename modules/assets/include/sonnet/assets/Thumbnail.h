#pragma once

#include <sonnet/assets/Asset.h>

#include <sonnet/core/Error.h>
#include <sonnet/core/Uuid.h>
#include <sonnet/renderer/Texture.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace sonnet::assets {

// The longest side of a thumbnail, in pixels (docs/assets.md, "Thumbnails").
inline constexpr std::uint32_t ThumbnailSize = 128;

// What making one asset's thumbnail needs, copied out of the database so a worker can run it.
struct ThumbnailRequest {
  core::Uuid uuid;
  AssetType type{AssetType::Texture}; // Texture, Environment or Material
  std::filesystem::path source;
  std::filesystem::path sidecar;
  std::filesystem::path cache; // the cache directory
  std::filesystem::file_time_type sourceTime;
  TextureSettings settings;
  // A material: the source files and sidecars of the textures it reads, which its picture is as old as.
  std::vector<std::filesystem::path> dependencies;
  // Whether `storeThumbnail` writes: not for a material with edits that are not saved, whose file the
  // picture would then outlive.
  bool store{true};
};

// The cache entry of an asset's thumbnail, `<cache>/<uuid>.thumb.ktx2`.
[[nodiscard]] std::filesystem::path thumbnailFile(const std::filesystem::path &cache, const core::Uuid &uuid);

// The cache entry's picture, when it is no older than the source, the sidecar and the dependencies;
// nothing when there is none, it is stale or unreadable. Pure, like makeThumbnail.
[[nodiscard]] std::optional<renderer::TextureData> cachedThumbnail(const ThumbnailRequest &request);

// Writes the picture of a request as its cache entry; does nothing when the request does not store.
[[nodiscard]] core::Result<void> storeThumbnail(const ThumbnailRequest &request, const renderer::TextureData &picture);

// A picture of a texture or an environment, one RGBA8 level of at most ThumbnailSize on its longest
// side, its bytes as ImGui shows them (sRGB-encoded, so the format is Unorm). Read from the cache
// entry when it is no older than the source and its sidecar, and made from the source and written
// there otherwise. A material has no source to make it from: the editor renders it
// ([editor.md](editor.md#thumbnails)), so asking for one is an error. Pure: touches nothing but the files, so it
// runs on any thread.
[[nodiscard]] core::Result<renderer::TextureData> makeThumbnail(const ThumbnailRequest &request);

} // namespace sonnet::assets
