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

// KHR_texture_transform: a slot samples at offset + rotate(rotation) * (scale * uv), the rotation in
// radians, counter-clockwise. The default is the identity.
struct TextureTransform {
  glm::vec2 offset{0.0f, 0.0f};
  float rotation{0.0f};
  glm::vec2 scale{1.0f, 1.0f};

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
  TextureWrap wrap{TextureWrap::Repeat};
  bool doubleSided{false};
};

} // namespace sonnet::renderer
