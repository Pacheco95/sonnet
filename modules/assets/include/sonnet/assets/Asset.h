#pragma once

#include <sonnet/core/Error.h>
#include <sonnet/core/Math.h>
#include <sonnet/core/Uuid.h>
#include <sonnet/renderer/Material.h>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sonnet::assets {

enum class AssetType : std::uint8_t {
  Texture,     // an image file, or an image inside a glTF file
  Mesh,        // a glTF mesh, or a built-in primitive
  Material,    // a .material.json file, or a glTF material
  Model,       // a glTF file: the node hierarchy over its meshes and materials
  Environment, // an equirectangular .hdr map
};

[[nodiscard]] std::string_view toString(AssetType type) noexcept;

// What the database knows about an asset before it is loaded (docs/assets.md, "Database").
struct AssetInfo {
  core::Uuid uuid;
  AssetType type{AssetType::Texture};
  std::filesystem::path source;      // the file, absolute; for a sub-asset, the parent's file
  std::string name;                  // the file's stem, or the sub-asset's own name
  core::Uuid parent;                 // nil for a file asset
  std::vector<core::Uuid> materials; // Mesh: the default material per slot, nil for none
};

// The import settings of an image, kept in its sidecar and shown in the inspector.
struct TextureSettings {
  bool srgb{true}; // colour data; off for normal, roughness, metallic and occlusion maps
  bool mipmaps{true};
  bool compress{true}; // cooked to UASTC, transcoded to BC7 on desktop; uncompressed RGBA8 otherwise

  [[nodiscard]] static TextureSettings fromJson(const nlohmann::json &json);
  [[nodiscard]] nlohmann::json toJson() const;
  bool operator==(const TextureSettings &) const = default;
};

// A material as authored: the renderer's description with textures named by asset instead of
// by handle (docs/assets.md, "Identity").
struct MaterialSource {
  glm::vec4 baseColor{1.0f, 1.0f, 1.0f, 1.0f};
  glm::vec3 emissive{0.0f, 0.0f, 0.0f};
  float metallic{1.0f};
  float roughness{1.0f};
  float normalScale{1.0f};
  float occlusionStrength{1.0f};
  float alphaCutoff{0.5f};
  core::Uuid baseColorTexture{};
  core::Uuid metallicRoughnessTexture{};
  core::Uuid normalTexture{};
  core::Uuid occlusionTexture{};
  core::Uuid emissiveTexture{};
  renderer::AlphaMode alphaMode{renderer::AlphaMode::Opaque};
  renderer::TextureWrap wrap{renderer::TextureWrap::Repeat};
  bool doubleSided{false};

  bool operator==(const MaterialSource &) const = default;
};

// The .material.json format, versioned like the scene files (docs/assets.md, "Materials").
constexpr int MaterialFileVersion = 1;
[[nodiscard]] nlohmann::json saveMaterial(const MaterialSource &material);
[[nodiscard]] core::Result<MaterialSource> loadMaterial(const nlohmann::json &document);

// The node hierarchy of a glTF file, which `world` turns into a prefab.
struct ModelNode {
  std::string name;
  std::int32_t parent{-1}; // index into the nodes, -1 for a root
  glm::vec3 position{0.0f};
  glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
  glm::vec3 scale{1.0f};
  core::Uuid mesh{}; // nil for a node without geometry
};

struct Model {
  std::vector<ModelNode> nodes; // parents before children
};

// The built-in primitive meshes, registered by every database under fixed identities so scenes
// without imported assets, and the starter scene, still have geometry.
namespace builtin {
[[nodiscard]] core::Uuid box() noexcept;
[[nodiscard]] core::Uuid sphere() noexcept;
[[nodiscard]] core::Uuid plane() noexcept;
[[nodiscard]] core::Uuid cylinder() noexcept;
[[nodiscard]] core::Uuid capsule() noexcept;
} // namespace builtin

} // namespace sonnet::assets
