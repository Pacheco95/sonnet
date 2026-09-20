#pragma once

#include <sonnet/assets/Animation.h>
#include <sonnet/assets/Asset.h>

#include <sonnet/core/Error.h>
#include <sonnet/core/Uuid.h>
#include <sonnet/renderer/Mesh.h>
#include <sonnet/renderer/Texture.h>

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sonnet::assets {

// The cooked form of a project (docs/assets.md, "Cooking and export"), one file: a header, the
// payload blobs back to back, and a CBOR index at the end holding the manifest, the assets by
// identity and the scene and prefab files by their project-relative path (ADR-0011).
constexpr std::uint32_t BundleVersion = 1;
constexpr std::string_view BundleExtension = ".sbundle";

// What a bundle is cooked for. The desktop platforms cook the same bytes and differ only in
// which player binary export copies; mobile joins in M9 with its own texture format.
enum class CookPlatform : std::uint8_t {
  Windows,
  Linux,
  MacOS,
};

[[nodiscard]] std::string_view toString(CookPlatform platform) noexcept;
[[nodiscard]] std::optional<CookPlatform> cookPlatformFromString(std::string_view name) noexcept;
// The platform the running binary was built for, the default an export offers.
[[nodiscard]] CookPlatform hostPlatform() noexcept;

struct BundleManifest {
  std::string name;
  std::string engineVersion;
  CookPlatform platform{CookPlatform::Linux};
  std::string startScene; // one of the file entries, "scenes/main.scene.json"
};

// What the index says about one asset: `AssetInfo` without a source path, which a cooked asset
// no longer has.
struct BundleAsset {
  core::Uuid uuid;
  AssetType type{AssetType::Texture};
  std::string name;
  core::Uuid parent;                 // nil for a file asset
  std::vector<core::Uuid> materials; // Mesh: the default material per slot
};

// Reads a bundle: the index at open, a payload when it is asked for. Cheap to keep open, since
// nothing but the index is held in memory.
class Bundle {
public:
  [[nodiscard]] static core::Result<Bundle> open(const std::filesystem::path &file);

  [[nodiscard]] const BundleManifest &manifest() const noexcept {
    return m_manifest;
  }
  [[nodiscard]] std::span<const BundleAsset> assets() const noexcept {
    return m_assets;
  }
  // The scene and prefab paths, sorted.
  [[nodiscard]] std::vector<std::string> files() const;
  [[nodiscard]] bool contains(const core::Uuid &uuid) const;
  [[nodiscard]] bool contains(std::string_view path) const;

  // A payload, read from the file. A missing entry or a short read is an Io error.
  [[nodiscard]] core::Result<std::vector<std::byte>> read(const core::Uuid &uuid) const;
  [[nodiscard]] core::Result<std::vector<std::byte>> read(std::string_view path) const;

  [[nodiscard]] const std::filesystem::path &path() const noexcept {
    return m_path;
  }

private:
  struct Span {
    std::uint64_t offset{0};
    std::uint64_t size{0};
  };

  [[nodiscard]] core::Result<std::vector<std::byte>> readSpan(const Span &span) const;

  std::filesystem::path m_path;
  mutable std::ifstream m_file;
  BundleManifest m_manifest;
  std::vector<BundleAsset> m_assets;
  std::unordered_map<core::Uuid, Span> m_assetSpans;
  std::map<std::string, Span, std::less<>> m_fileSpans;
};

// Writes a bundle, streaming each payload out as it arrives so a project's textures never all
// sit in memory. `finish` writes the index and the header; a writer destroyed without it leaves
// a file that `Bundle::open` rejects.
class BundleWriter {
public:
  [[nodiscard]] static core::Result<BundleWriter> create(const std::filesystem::path &file, BundleManifest manifest);
  BundleWriter(BundleWriter &&) noexcept = default;
  BundleWriter &operator=(BundleWriter &&) noexcept = default;
  BundleWriter(const BundleWriter &) = delete;
  BundleWriter &operator=(const BundleWriter &) = delete;

  [[nodiscard]] core::Result<void> addAsset(const BundleAsset &asset, std::span<const std::byte> payload);
  [[nodiscard]] core::Result<void> addFile(std::string path, std::span<const std::byte> payload);
  [[nodiscard]] core::Result<void> finish();

  [[nodiscard]] std::uint64_t bytesWritten() const noexcept {
    return m_offset;
  }

private:
  BundleWriter() = default;

  [[nodiscard]] core::Result<void> append(std::span<const std::byte> payload, std::uint64_t &offset);

  std::filesystem::path m_path;
  std::ofstream m_file;
  BundleManifest m_manifest;
  nlohmann::json m_assets = nlohmann::json::array();
  nlohmann::json m_files = nlohmann::json::object();
  std::uint64_t m_offset{0};
};

// The payload encodings. Textures keep the KTX2 bytes the editor already caches, scripts their
// source and sounds their encoded file, so those three need no function here; the rest are
// below. Every decode is defensive: a truncated or foreign payload is an Io error, never a read
// past the end.
[[nodiscard]] std::vector<std::byte> encodeMesh(const renderer::MeshData &mesh);
[[nodiscard]] core::Result<renderer::MeshData> decodeMesh(std::span<const std::byte> payload);

// Uncompressed texture data with its description, for the equirectangular maps behind
// environments, which are RGBA16F and so not something the KTX2 path cooks.
[[nodiscard]] std::vector<std::byte> encodeTexture(const renderer::TextureData &texture);
[[nodiscard]] core::Result<renderer::TextureData> decodeTexture(std::span<const std::byte> payload);

[[nodiscard]] std::vector<std::byte> encodeSkin(const Skin &skin);
[[nodiscard]] core::Result<Skin> decodeSkin(std::span<const std::byte> payload);

[[nodiscard]] std::vector<std::byte> encodeAnimation(const AnimationClip &clip);
[[nodiscard]] core::Result<AnimationClip> decodeAnimation(std::span<const std::byte> payload);

[[nodiscard]] std::vector<std::byte> encodeModel(const Model &model);
[[nodiscard]] core::Result<Model> decodeModel(std::span<const std::byte> payload);

// Materials, scenes and prefabs are the JSON they already are, in CBOR: the version field and
// the migrations keep working, and a cooked scene can be dumped back to JSON to look at.
[[nodiscard]] std::vector<std::byte> encodeJson(const nlohmann::json &document);
[[nodiscard]] core::Result<nlohmann::json> decodeJson(std::span<const std::byte> payload);

} // namespace sonnet::assets
