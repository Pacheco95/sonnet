#pragma once

#include <sonnet/assets/Animation.h>
#include <sonnet/assets/Asset.h>

#include <sonnet/core/Error.h>
#include <sonnet/renderer/Mesh.h>
#include <sonnet/renderer/Texture.h>
#include <sonnet/rhi/Device.h>

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

// A KTX2 file: Basis Universal data is transcoded to BC7 when the device has block compression,
// to ASTC 4x4 when it has ASTC instead, and to RGBA8 otherwise; UASTC HDR data to BC6H, ASTC HDR
// 4x4 or RGBA16F by the same rule; other formats are taken as they are. A format the device cannot sample is an error,
// which the caller treats as a failed import.
[[nodiscard]] core::Result<renderer::TextureData> readKtx2(std::span<const std::byte> bytes,
                                                           const rhi::DeviceInfo &device);
// A Basis Universal KTX2 file, as a glTF file's KHR_texture_basisu image is, copied with its
// transfer function set to sRGB or linear, so readKtx2 transcodes it to the colour space the
// material slot reads rather than the one the file names. The data is not touched; any other KTX2
// file is an error.
[[nodiscard]] core::Result<std::vector<std::byte>> markKtx2(std::span<const std::byte> bytes, bool srgb);
// An RGBA8 texture cooked into a KTX2 file: UASTC with zstd supercompression when `compress`
// is set, uncompressed otherwise (docs/assets.md, "Textures").
[[nodiscard]] core::Result<std::vector<std::byte>> cookKtx2(const renderer::TextureData &texture, bool compress);
// An equirectangular RGBA16F map cooked into a KTX2 file as UASTC HDR 4x4 with zstd supercompression,
// the one portable form of an environment: readKtx2 transcodes it by device (docs/assets.md,
// "Environments"). The encoding keeps no alpha.
[[nodiscard]] core::Result<std::vector<std::byte>> cookHdrKtx2(const renderer::TextureData &texture);
// An RGBA8 texture encoded as ASTC for a mobile bundle (ADR-0018): 6x6 in perceptual mode for an
// sRGB texture, 4x4 for a linear one, at astcenc's medium quality, every level encoded from the
// level given rather than from a lossy form of it.
[[nodiscard]] core::Result<std::vector<std::byte>> cookAstcKtx2(const renderer::TextureData &texture);

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
  std::vector<std::byte> bytes; // the encoded file: PNG or JPEG, or KTX2 when `ktx2`
  bool srgb{true};              // used as colour by some material
  bool ktx2{false};             // a KHR_texture_basisu image, used as it is (markKtx2)
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

// A glTF file's node hierarchy without its payloads: what a prefab of the model needs, read from
// the JSON without loading a buffer or an image. The nodes are importGltf's, index for index.
struct GltfStructure {
  Model model;                           // mesh and skin identities and the clips left empty
  std::vector<std::int32_t> meshIndices; // per model node, -1 for none
  std::vector<std::int32_t> skinIndices; // per model node, -1 for none
  std::size_t animationCount{0};
};

[[nodiscard]] core::Result<GltfStructure> importGltfStructure(const std::filesystem::path &path);

// The files a glTF file names by relative URI, its external buffers and images, relative to the
// file's folder and without repeats; data URIs and a .glb file's own chunk are not files. A URI
// that leaves the folder is an error, since the file would not survive being moved with them.
[[nodiscard]] core::Result<std::vector<std::filesystem::path>> gltfDependencies(const std::filesystem::path &path);

} // namespace sonnet::assets
