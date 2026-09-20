#pragma once

#include <sonnet/assets/Animation.h>
#include <sonnet/assets/Asset.h>

#include <sonnet/core/Error.h>
#include <sonnet/renderer/Mesh.h>
#include <sonnet/renderer/Texture.h>

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace sonnet::assets {

// The importers behind the database (docs/assets.md, "Importers"), usable on their own.

// PNG, JPEG and the other formats stb_image reads, decoded to RGBA8 with the settings' colour
// space and mip chain.
[[nodiscard]] core::Result<renderer::TextureData> importImage(std::span<const std::byte> bytes,
                                                              const TextureSettings &settings);
// Radiance .hdr, decoded to RGBA16F for an environment.
[[nodiscard]] core::Result<renderer::TextureData> importHdr(std::span<const std::byte> bytes);

// A KTX2 file: Basis Universal data is transcoded to BC7 when the device supports block
// compression, to RGBA8 otherwise; other formats are taken as they are.
[[nodiscard]] core::Result<renderer::TextureData> readKtx2(std::span<const std::byte> bytes, bool blockCompression);
// An RGBA8 texture cooked into a KTX2 file: UASTC with zstd supercompression when `compress`
// is set, uncompressed otherwise (docs/assets.md, "Textures").
[[nodiscard]] core::Result<std::vector<std::byte>> cookKtx2(const renderer::TextureData &texture, bool compress);

// What a glTF file yields. Materials and meshes refer to each other by index; the database
// gives every one a derived identity.
struct GltfMesh {
  std::string name;
  renderer::MeshData data;
  std::vector<std::int32_t> materials; // per submesh slot, -1 for the default
};

struct GltfMaterial {
  std::string name;
  MaterialSource source;           // texture identities left nil
  std::int32_t baseColorImage{-1}; // image indices, -1 for none
  std::int32_t metallicRoughnessImage{-1};
  std::int32_t normalImage{-1};
  std::int32_t occlusionImage{-1};
  std::int32_t emissiveImage{-1};
};

struct GltfImage {
  std::string name;
  std::vector<std::byte> bytes; // the encoded file, PNG or JPEG
  bool srgb{true};              // used as colour by some material
};

// Joints and channels name their nodes by path from the model's root (Animation.h).
struct GltfSkin {
  std::string name;
  Skin skin;
};

struct GltfAnimation {
  std::string name;
  AnimationClip clip;
};

struct GltfImport {
  std::vector<GltfMesh> meshes;
  std::vector<GltfMaterial> materials;
  std::vector<GltfImage> images;
  std::vector<GltfSkin> skins;
  std::vector<GltfAnimation> animations;
  Model model;                           // nodes refer to meshes and skins through the index lists
  std::vector<std::int32_t> meshIndices; // per model node, -1 for none
  std::vector<std::int32_t> skinIndices; // per model node, -1 for none
};

[[nodiscard]] core::Result<GltfImport> importGltf(const std::filesystem::path &path);

} // namespace sonnet::assets
