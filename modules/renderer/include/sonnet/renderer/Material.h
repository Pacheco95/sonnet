#pragma once

#include <sonnet/renderer/Texture.h>

#include <sonnet/core/Handle.h>
#include <sonnet/core/Math.h>

#include <cstdint>

namespace sonnet::renderer {

struct MaterialTag {};
using MaterialHandle = core::Handle<MaterialTag>;

enum class AlphaMode : std::uint8_t {
  Opaque,
  Mask,  // discards below the cutoff, in the depth pre-pass and the shadows too
  Blend, // drawn after the opaque scene, sorted back to front, without depth writes
};

enum class TextureWrap : std::uint8_t {
  Repeat,
  ClampToEdge,
  MirroredRepeat,
};

enum class TextureFilter : std::uint8_t {
  Nearest,
  Linear,
};
enum class TextureMipFilter : std::uint8_t {
  Nearest,
  Linear,
  None,
};

struct TextureSampler {
  TextureWrap wrapS{TextureWrap::Repeat};
  TextureWrap wrapT{TextureWrap::Repeat};
  TextureFilter magFilter{TextureFilter::Linear};
  TextureFilter minFilter{TextureFilter::Linear};
  TextureMipFilter mipFilter{TextureMipFilter::Linear};
  bool operator==(const TextureSampler &) const = default;
};

// How a texture slot reads its UVs: the vertex UV set it takes (glTF's texCoord, 0 for `Vertex::uv`
// and 1 for `Vertex::uv1`), then KHR_texture_transform, sampling at
// offset + rotate(rotation) * (scale * uv) with the rotation in radians, counter-clockwise. The
// default is set 0 untransformed.
struct TextureTransform {
  glm::vec2 offset{0.0f, 0.0f};
  float rotation{0.0f};
  glm::vec2 scale{1.0f, 1.0f};
  std::uint32_t texCoord{0};

  bool operator==(const TextureTransform &) const = default;
};

// The glTF metallic-roughness model. An invalid texture handle means the neutral default:
// white for colours and factors, a flat normal for the normal map.
struct MaterialDesc {
  glm::vec4 baseColor{1.0f, 1.0f, 1.0f, 1.0f};
  glm::vec3 emissive{0.0f, 0.0f, 0.0f};
  float metallic{1.0f};
  float roughness{1.0f};
  float normalScale{1.0f};
  float occlusionStrength{1.0f};
  float alphaCutoff{0.5f};
  TextureHandle baseColorTexture{};
  TextureHandle metallicRoughnessTexture{}; // roughness in green, metallic in blue
  TextureHandle normalTexture{};
  TextureHandle occlusionTexture{}; // red
  TextureHandle emissiveTexture{};
  TextureTransform baseColorTransform{};
  TextureTransform metallicRoughnessTransform{};
  TextureTransform normalTransform{};
  TextureTransform occlusionTransform{};
  TextureTransform emissiveTransform{};
  AlphaMode alphaMode{AlphaMode::Opaque};
  TextureSampler baseColorSampler{};
  TextureSampler metallicRoughnessSampler{};
  TextureSampler normalSampler{};
  TextureSampler occlusionSampler{};
  TextureSampler emissiveSampler{};
  bool doubleSided{false};
  bool unlit{false}; // KHR_materials_unlit: base colour plus emissive, no lighting
};

} // namespace sonnet::renderer
