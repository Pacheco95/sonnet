#include <sonnet/assets/Importers.h>

#include <sonnet/core/Log.h>
#include <sonnet/core/Profile.h>

#include <ktx.h>

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
constexpr std::uint32_t VkFormatBc5Unorm = 141;
constexpr std::uint32_t VkFormatBc7Unorm = 145;
constexpr std::uint32_t VkFormatBc7Srgb = 146;

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
  case VkFormatBc7Unorm:
    return rhi::Format::BC7Unorm;
  case VkFormatBc7Srgb:
    return rhi::Format::BC7Srgb;
  default:
    return rhi::Format::Undefined;
  }
}

struct KtxDestroy {
  void operator()(ktxTexture2 *texture) const noexcept {
    ktxTexture_Destroy(ktxTexture(texture));
  }
};
using KtxTexture = std::unique_ptr<ktxTexture2, KtxDestroy>;

core::Error ktxError(std::string_view what, ktx_error_code_e code) {
  return core::Error{std::format("{}: {}", what, ktxErrorString(code)), core::ErrorCategory::Io};
}

} // namespace

core::Result<renderer::TextureData> readKtx2(std::span<const std::byte> bytes, bool blockCompression) {
  SONNET_ZONE();
  ktxTexture2 *raw = nullptr;
  ktx_error_code_e result = ktxTexture2_CreateFromMemory(reinterpret_cast<const ktx_uint8_t *>(bytes.data()),
                                                         bytes.size(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &raw);
  if (result != KTX_SUCCESS) {
    return std::unexpected(ktxError("reading the KTX2 file", result));
  }
  KtxTexture texture{raw};
  if (ktxTexture2_NeedsTranscoding(texture.get())) {
    result = ktxTexture2_TranscodeBasis(texture.get(), blockCompression ? KTX_TTF_BC7_RGBA : KTX_TTF_RGBA32, 0);
    if (result != KTX_SUCCESS) {
      return std::unexpected(ktxError("transcoding the KTX2 file", result));
    }
  }
  const rhi::Format format = fromVkFormat(texture->vkFormat);
  if (format == rhi::Format::Undefined) {
    return std::unexpected(core::Error{std::format("KTX2 format {} is not one the engine reads", texture->vkFormat),
                                       core::ErrorCategory::Io});
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
      result = ktxTexture_GetImageOffset(ktxTexture(texture.get()), level, 0, face, &offset);
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
  if (source.format != rhi::Format::R8G8B8A8Unorm && source.format != rhi::Format::R8G8B8A8Srgb) {
    return std::unexpected(core::Error{"only RGBA8 textures are cooked", core::ErrorCategory::Io});
  }
  if (source.data.size() != source.expectedSize()) {
    return std::unexpected(core::Error{"the texture data does not match its description", core::ErrorCategory::Io});
  }
  ktxTextureCreateInfo info{};
  info.vkFormat = source.format == rhi::Format::R8G8B8A8Srgb ? VkFormatR8G8B8A8Srgb : VkFormatR8G8B8A8Unorm;
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
      result = ktxTexture_SetImageFromMemory(ktxTexture(texture.get()), level, 0, face,
                                             reinterpret_cast<const ktx_uint8_t *>(image.data()), image.size());
      if (result != KTX_SUCCESS) {
        return std::unexpected(ktxError(std::format("setting level {} of the KTX2 texture", level), result));
      }
    }
  }
  if (compress) {
    // UASTC keeps quality high and transcodes to BC7 and ASTC alike (docs/assets.md, "Textures");
    // the faster level is a good trade for an editor that cooks on demand.
    ktxBasisParams params{};
    params.structSize = sizeof(params);
    params.uastc = KTX_TRUE;
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
  ktx_uint8_t *bytes = nullptr;
  ktx_size_t size = 0;
  result = ktxTexture_WriteToMemory(ktxTexture(texture.get()), &bytes, &size);
  if (result != KTX_SUCCESS) {
    return std::unexpected(ktxError("writing the KTX2 file", result));
  }
  std::vector<std::byte> out(reinterpret_cast<const std::byte *>(bytes),
                             reinterpret_cast<const std::byte *>(bytes) + size);
  std::free(bytes);
  return out;
}

} // namespace sonnet::assets
