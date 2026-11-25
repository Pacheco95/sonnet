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

// How a lit material shades (ADR-0041, ADR-0046). Skin is pre-integrated subsurface scattering in the
// forward pass and Hair two shifted Kajiya-Kay lobes along the strand texture's direction, each drawn
// by a forward pipeline of its own; an opaque or masked draw only, so a blended, transmissive or
// unlit Skin or Hair material shades as Standard, and so does every one of them while
// RendererSettings::characterShading is off. glTF materials are always Standard.
enum class ShadingModel : std::uint8_t {
  Standard,
  Skin,
  Hair,
};

// The bounds of MaterialDesc::subsurfaceRadius, in metres per millimetre of the skin profile.
constexpr float MaxSubsurfaceRadius = 0.01f;

// The bounds of Hair's lobe fields (MaterialDesc::hairShift and the three after it).
constexpr float MaxHairShift = 1.0f;
constexpr float MinHairSecondaryWidth = 0.05f;
constexpr float MaxHairSecondaryStrength = 2.0f;

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
  float transmission{0.0f}; // KHR_materials_transmission, 0 to 1 (ADR-0033)
  TextureHandle baseColorTexture{};
  TextureHandle metallicRoughnessTexture{}; // roughness in green, metallic in blue
  TextureHandle normalTexture{};
  TextureHandle occlusionTexture{}; // red
  TextureHandle emissiveTexture{};
  TextureHandle transmissionTexture{}; // red
  // Hair's strand direction (ADR-0046), a linear data texture: the strand's axis as a doubled angle,
  // cos 2a in red and sin 2a in green, in the texture's UV frame, and a shift in blue. Read only by
  // the Hair model. An invalid handle is the neutral strand: along U, without a shift.
  TextureHandle strandTexture{};
  TextureTransform baseColorTransform{};
  TextureTransform metallicRoughnessTransform{};
  TextureTransform normalTransform{};
  TextureTransform occlusionTransform{};
  TextureTransform emissiveTransform{};
  TextureTransform transmissionTransform{};
  TextureTransform strandTransform{};
  AlphaMode alphaMode{AlphaMode::Opaque};
  TextureSampler baseColorSampler{};
  TextureSampler metallicRoughnessSampler{};
  TextureSampler normalSampler{};
  TextureSampler occlusionSampler{};
  TextureSampler emissiveSampler{};
  TextureSampler transmissionSampler{};
  TextureSampler strandSampler{};
  bool doubleSided{false};
  bool unlit{false}; // KHR_materials_unlit: base colour plus emissive, no lighting
  ShadingModel shading{ShadingModel::Standard};
  // Skin's: the weight of the scattering, 0 to 1 (0 shades as Standard), and the metres one
  // millimetre of d'Eon's skin profile stands for, 0 to MaxSubsurfaceRadius (0.001 is human skin).
  float subsurface{1.0f};
  float subsurfaceRadius{0.001f};
  // Hair's lobes (ADR-0046), each shifted along the normal by this much plus the strand texture's shift:
  // the primary's (white) and the secondary's (tinted by the base colour), the secondary's exponent as
  // a fraction of the primary's (wider below 1), and the secondary's weight. Within the bounds above.
  float hairShift{-0.08f};
  float hairSecondaryShift{0.12f};
  float hairSecondaryWidth{0.5f};
  float hairSecondaryStrength{0.5f};
};

// Whether a material's draws can let light through, and so take the transmission path (ADR-0033,
// "Classification"): lit, a transmission above 0, and an effective transmission the CPU cannot prove
// zero. Only a metallic factor of 1 without a metallic-roughness texture proves it; textures are not
// scanned, so a texture-dependent material counts as transmissive.
[[nodiscard]] constexpr bool mayTransmit(const MaterialDesc &desc) noexcept {
  return !desc.unlit && desc.transmission > 0.0f && (desc.metallic < 1.0f || desc.metallicRoughnessTexture.isValid());
}

} // namespace sonnet::renderer
