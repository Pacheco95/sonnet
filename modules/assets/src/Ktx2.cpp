#include <sonnet/assets/Importers.h>

#include <sonnet/core/Log.h>
#include <sonnet/core/Profile.h>

#include <ktx.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <format>
#include <memory>
#include <thread>

namespace sonnet::assets {

namespace {

// KTX2 names formats by their Vulkan numbers (the KTX 2.0 specification, "vkFormat"); these are
// the ones the engine reads and writes, spelled out so the module stays free of Vulkan headers.
constexpr std::uint32_t VkFormatR8G8B8A8Unorm = 37;
constexpr std::uint32_t VkFormatR8G8B8A8Srgb = 43;
constexpr std::uint32_t VkFormatR16G16B16A16Sfloat = 97;
constexpr std::uint32_t VkFormatBc4Unorm = 139;
constexpr std::uint32_t VkFormatBc6hUfloat = 143;
constexpr std::uint32_t VkFormatBc5Unorm = 141;
constexpr std::uint32_t VkFormatBc7Unorm = 145;
constexpr std::uint32_t VkFormatBc7Srgb = 146;
constexpr std::uint32_t VkFormatAstc4x4Unorm = 157;
constexpr std::uint32_t VkFormatAstc4x4Srgb = 158;
constexpr std::uint32_t VkFormatAstc6x6Unorm = 165;
constexpr std::uint32_t VkFormatAstc6x6Srgb = 166;
constexpr std::uint32_t VkFormatAstc4x4Sfloat = 1000066000; // VK_EXT_texture_compression_astc_hdr

rhi::Format fromVkFormat(std::uint32_t format) noexcept {
  switch (format) {
  case VkFormatR8G8B8A8Unorm:
    return rhi::Format::R8G8B8A8Unorm;
  case VkFormatR8G8B8A8Srgb:
    return rhi::Format::R8G8B8A8Srgb;
  case VkFormatR16G16B16A16Sfloat:
    return rhi::Format::R16G16B16A16Sfloat;
  case VkFormatBc4Unorm:
    return rhi::Format::BC4Unorm;
  case VkFormatBc5Unorm:
    return rhi::Format::BC5Unorm;
  case VkFormatBc6hUfloat:
    return rhi::Format::BC6HUfloat;
  case VkFormatBc7Unorm:
    return rhi::Format::BC7Unorm;
  case VkFormatBc7Srgb:
    return rhi::Format::BC7Srgb;
  case VkFormatAstc4x4Unorm:
    return rhi::Format::ASTC4x4Unorm;
  case VkFormatAstc4x4Srgb:
    return rhi::Format::ASTC4x4Srgb;
  case VkFormatAstc6x6Unorm:
    return rhi::Format::ASTC6x6Unorm;
  case VkFormatAstc6x6Srgb:
    return rhi::Format::ASTC6x6Srgb;
  case VkFormatAstc4x4Sfloat:
    return rhi::Format::ASTC4x4Sfloat;
  default:
    return rhi::Format::Undefined;
  }
}

// The library's ktxTexture() macro is a C-style cast to the base struct.
ktxTexture *asBase(ktxTexture2 *texture) noexcept {
  return reinterpret_cast<ktxTexture *>(texture);
}

struct KtxDestroy {
  void operator()(ktxTexture2 *texture) const noexcept {
    ktxTexture_Destroy(asBase(texture));
  }
};
using KtxTexture = std::unique_ptr<ktxTexture2, KtxDestroy>;

core::Error ktxError(std::string_view what, ktx_error_code_e code) {
  return core::Error{std::format("{}: {}", what, ktxErrorString(code)), core::ErrorCategory::Io};
}

// An RGBA8 or RGBA16F texture as an uncompressed KTX2 texture with the same levels, the input the
// encoders take.
core::Result<KtxTexture> createKtx2(const renderer::TextureData &source) {
  const bool hdr = source.format == rhi::Format::R16G16B16A16Sfloat;
  if (!hdr && source.format != rhi::Format::R8G8B8A8Unorm && source.format != rhi::Format::R8G8B8A8Srgb) {
    return std::unexpected(core::Error{"only RGBA8 and RGBA16F textures are cooked", core::ErrorCategory::Io});
  }
  if (source.data.size() != source.expectedSize()) {
    return std::unexpected(core::Error{"the texture data does not match its description", core::ErrorCategory::Io});
  }
  ktxTextureCreateInfo info{};
  info.vkFormat = hdr                                          ? VkFormatR16G16B16A16Sfloat
                  : source.format == rhi::Format::R8G8B8A8Srgb ? VkFormatR8G8B8A8Srgb
                                                               : VkFormatR8G8B8A8Unorm;
  info.baseWidth = source.size.x;
  info.baseHeight = source.size.y;
  info.baseDepth = 1;
  info.numDimensions = 2;
  info.numLevels = source.mipLevels;
  info.numLayers = 1;
  info.numFaces = source.layers();
  info.isArray = KTX_FALSE;
  info.generateMipmaps = KTX_FALSE;
  ktxTexture2 *raw = nullptr;
  ktx_error_code_e result = ktxTexture2_Create(&info, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &raw);
  if (result != KTX_SUCCESS) {
    return std::unexpected(ktxError("creating the KTX2 texture", result));
  }
  KtxTexture texture{raw};
  for (std::uint32_t level = 0; level < source.mipLevels; ++level) {
    for (std::uint32_t face = 0; face < source.layers(); ++face) {
      const std::span<const std::byte> image = source.level(level, face);
      result = ktxTexture_SetImageFromMemory(asBase(texture.get()), level, 0, face,
                                             reinterpret_cast<const ktx_uint8_t *>(image.data()), image.size());
      if (result != KTX_SUCCESS) {
        return std::unexpected(ktxError(std::format("setting level {} of the KTX2 texture", level), result));
      }
    }
  }
  return texture;
}

core::Result<std::vector<std::byte>> writeKtx2(ktxTexture2 *texture) {
  ktx_uint8_t *bytes = nullptr;
  ktx_size_t size = 0;
  const ktx_error_code_e result = ktxTexture_WriteToMemory(asBase(texture), &bytes, &size);
  if (result != KTX_SUCCESS) {
    return std::unexpected(ktxError("writing the KTX2 file", result));
  }
  std::vector<std::byte> out(reinterpret_cast<const std::byte *>(bytes),
                             reinterpret_cast<const std::byte *>(bytes) + size);
  std::free(bytes);
  return out;
}

} // namespace

core::Result<renderer::TextureData> readKtx2(std::span<const std::byte> bytes, const rhi::DeviceInfo &device) {
  SONNET_ZONE();
  ktxTexture2 *raw = nullptr;
  ktx_error_code_e result = ktxTexture2_CreateFromMemory(reinterpret_cast<const ktx_uint8_t *>(bytes.data()),
                                                         bytes.size(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &raw);
  if (result != KTX_SUCCESS) {
    return std::unexpected(ktxError("reading the KTX2 file", result));
  }
  KtxTexture texture{raw};
  // UASTC HDR is stored as the ASTC HDR blocks it is a subset of, so the library calls it ready to
  // sample; its colour model says what it is, and a device without ASTC HDR needs it transcoded.
  const bool hdr = ktxTexture2_GetColorModel_e(texture.get()) == KHR_DF_MODEL_UASTC_HDR_4x4;
  if (hdr || ktxTexture2_NeedsTranscoding(texture.get())) {
    // UASTC is a restricted ASTC 4x4, so a phone without BC takes it at 8 bits per texel rather
    // than the 32 of RGBA8 (docs/assets.md, "Textures"). UASTC HDR is the same for HDR colour,
    // with BC6H, ASTC HDR and RGBA16F in the places of BC7, ASTC and RGBA8 ("Environments").
    ktx_transcode_fmt_e target = KTX_TTF_RGBA32;
    if (hdr) {
      target = device.bc6hSupported      ? KTX_TTF_BC6HU_RGB
               : device.astcHdrSupported ? KTX_TTF_ASTC_HDR_4x4_RGBA
                                         : KTX_TTF_RGBA_HALF;
    } else if (device.blockCompressionSupported) {
      target = KTX_TTF_BC7_RGBA;
    } else if (device.astcSupported) {
      target = KTX_TTF_ASTC_4x4_RGBA;
    }
    result = ktxTexture2_TranscodeBasis(texture.get(), target, 0);
    if (result != KTX_SUCCESS) {
      return std::unexpected(ktxError("transcoding the KTX2 file", result));
    }
  }
  const rhi::Format format = fromVkFormat(texture->vkFormat);
  if (format == rhi::Format::Undefined) {
    return std::unexpected(core::Error{std::format("KTX2 format {} is not one the engine reads", texture->vkFormat),
                                       core::ErrorCategory::Io});
  }
  if (!rhi::formatSupported(device, format)) {
    return std::unexpected(core::Error{
        std::format("KTX2 format {} is not one the device samples", rhi::toString(format)), core::ErrorCategory::Io});
  }
  if (texture->numDimensions != 2 || texture->numLayers != 1 || (texture->numFaces != 1 && texture->numFaces != 6)) {
    return std::unexpected(core::Error{"KTX2 file is not a 2D image or a cube map", core::ErrorCategory::Io});
  }
  renderer::TextureData data{.size = {texture->baseWidth, texture->baseHeight},
                             .format = format,
                             .mipLevels = texture->numLevels,
                             .cube = texture->numFaces == 6,
                             .data = {}};
  data.data.reserve(static_cast<std::size_t>(data.expectedSize()));
  for (std::uint32_t level = 0; level < texture->numLevels; ++level) {
    const std::uint64_t levelBytes = rhi::levelByteSize(format, rhi::mipSize(data.size, level));
    for (std::uint32_t face = 0; face < texture->numFaces; ++face) {
      ktx_size_t offset = 0;
      result = ktxTexture_GetImageOffset(asBase(texture.get()), level, 0, face, &offset);
      if (result != KTX_SUCCESS || offset + levelBytes > texture->dataSize) {
        return std::unexpected(ktxError(std::format("reading level {} of the KTX2 file", level), result));
      }
      const auto *source = reinterpret_cast<const std::byte *>(texture->pData) + offset;
      data.data.insert(data.data.end(), source, source + levelBytes);
    }
  }
  return data;
}

core::Result<std::vector<std::byte>> cookKtx2(const renderer::TextureData &source, bool compress) {
  SONNET_ZONE();
  auto created = createKtx2(source);
  if (!created) {
    return std::unexpected(created.error());
  }
  KtxTexture texture = std::move(*created);
  ktx_error_code_e result = KTX_SUCCESS;
  if (compress) {
    // UASTC keeps quality high and transcodes to BC7 and ASTC alike (docs/assets.md, "Textures");
    // the faster level is a good trade for an editor that cooks on demand.
    ktxBasisParams params{};
    params.structSize = sizeof(params);
    params.codec = KTX_BASIS_CODEC_UASTC_LDR_4x4;
    params.threadCount = std::max(1u, std::thread::hardware_concurrency());
    params.uastcFlags = KTX_PACK_UASTC_LEVEL_FASTER;
    result = ktxTexture2_CompressBasisEx(texture.get(), &params);
    if (result != KTX_SUCCESS) {
      return std::unexpected(ktxError("compressing the KTX2 texture", result));
    }
    result = ktxTexture2_DeflateZstd(texture.get(), 10);
    if (result != KTX_SUCCESS) {
      return std::unexpected(ktxError("supercompressing the KTX2 texture", result));
    }
  }
  return writeKtx2(texture.get());
}

core::Result<std::vector<std::byte>> cookHdrKtx2(const renderer::TextureData &source) {
  SONNET_ZONE();
  if (source.format != rhi::Format::R16G16B16A16Sfloat || source.cube || source.mipLevels != 1) {
    return std::unexpected(core::Error{"only a single RGBA16F image is cooked as UASTC HDR", core::ErrorCategory::Io});
  }
  auto created = createKtx2(source);
  if (!created) {
    return std::unexpected(created.error());
  }
  KtxTexture texture = std::move(*created);
  // UASTC HDR 4x4 is a 24-mode subset of ASTC HDR 4x4 that transcodes to BC6H with very little
  // loss (docs/assets.md, "Environments"); it keeps no alpha. Level 2 is a good trade for a cook
  // that runs once per environment.
  ktxBasisParams params{};
  params.structSize = sizeof(params);
  params.codec = KTX_BASIS_CODEC_UASTC_HDR_4x4;
  params.threadCount = std::max(1u, std::thread::hardware_concurrency());
  params.uastcHDRQuality = 2;
  ktx_error_code_e result = ktxTexture2_CompressBasisEx(texture.get(), &params);
  if (result != KTX_SUCCESS) {
    return std::unexpected(ktxError("compressing the HDR KTX2 texture", result));
  }
  result = ktxTexture2_DeflateZstd(texture.get(), 10);
  if (result != KTX_SUCCESS) {
    return std::unexpected(ktxError("supercompressing the HDR KTX2 texture", result));
  }
  return writeKtx2(texture.get());
}

core::Result<std::vector<std::byte>> cookAstcKtx2(const renderer::TextureData &source) {
  SONNET_ZONE();
  auto created = createKtx2(source);
  if (!created) {
    return std::unexpected(created.error());
  }
  KtxTexture texture = std::move(*created);
  // ADR-0018, "Textures": colour at 6x6 in perceptual mode, data at 4x4, the size BC7 has on
  // desktop, since its error becomes shading error. No normal-map mode: forward.slang reads a
  // normal map's three channels, and that mode keeps two.
  const bool srgb = source.format == rhi::Format::R8G8B8A8Srgb;
  ktxAstcParams params{};
  params.structSize = sizeof(params);
  params.threadCount = std::max(1u, std::thread::hardware_concurrency());
  params.blockDimension = srgb ? KTX_PACK_ASTC_BLOCK_DIMENSION_6x6 : KTX_PACK_ASTC_BLOCK_DIMENSION_4x4;
  params.mode = KTX_PACK_ASTC_ENCODER_MODE_LDR;
  params.qualityLevel = KTX_PACK_ASTC_QUALITY_LEVEL_MEDIUM;
  params.perceptual = srgb ? KTX_TRUE : KTX_FALSE;
  const ktx_error_code_e result = ktxTexture2_CompressAstcEx(texture.get(), &params);
  if (result != KTX_SUCCESS) {
    return std::unexpected(ktxError("encoding the KTX2 texture as ASTC", result));
  }
  return writeKtx2(texture.get());
}

} // namespace sonnet::assets
