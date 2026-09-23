#pragma once

#include <sonnet/core/Handle.h>
#include <sonnet/core/Math.h>

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

namespace sonnet::rhi {

struct BufferTag {};
struct ImageTag {};
struct SamplerTag {};
struct ShaderTag {};
struct PipelineTag {};
using BufferHandle = core::Handle<BufferTag>;
using ImageHandle = core::Handle<ImageTag>;
using SamplerHandle = core::Handle<SamplerTag>;
using ShaderHandle = core::Handle<ShaderTag>;
using PipelineHandle = core::Handle<PipelineTag>;

enum class Format : std::uint8_t {
  Undefined,
  R8Unorm,
  R8G8B8A8Unorm,
  R8G8B8A8Srgb,
  B8G8R8A8Unorm,
  B8G8R8A8Srgb,
  R16G16Sfloat,       // the BRDF lookup table
  R16G16B16A16Sfloat, // HDR scene colour and environment maps
  R32Uint,            // entity ids for picking and the selection outline
  BC4Unorm,           // one channel, cooked textures
  BC5Unorm,           // two channels, cooked normal maps
  BC7Unorm,           // colour, cooked textures
  BC7Srgb,
  D32Sfloat,
};

[[nodiscard]] constexpr bool isDepthFormat(Format format) noexcept {
  return format == Format::D32Sfloat;
}

// Integer formats take their clear colour as integers, not normalised floats.
[[nodiscard]] constexpr bool isUintFormat(Format format) noexcept {
  return format == Format::R32Uint;
}

// Texel block dimensions and size: 1x1 blocks for plain formats, 4x4 for the compressed ones.
struct FormatInfo {
  std::uint32_t blockWidth{1};
  std::uint32_t blockHeight{1};
  std::uint32_t bytesPerBlock{0};
};

[[nodiscard]] constexpr FormatInfo formatInfo(Format format) noexcept {
  switch (format) {
  case Format::Undefined:
    return {1, 1, 0};
  case Format::R8Unorm:
    return {1, 1, 1};
  case Format::R8G8B8A8Unorm:
  case Format::R8G8B8A8Srgb:
  case Format::B8G8R8A8Unorm:
  case Format::B8G8R8A8Srgb:
  case Format::R16G16Sfloat:
  case Format::R32Uint:
  case Format::D32Sfloat:
    return {1, 1, 4};
  case Format::R16G16B16A16Sfloat:
    return {1, 1, 8};
  case Format::BC4Unorm:
    return {4, 4, 8};
  case Format::BC5Unorm:
  case Format::BC7Unorm:
  case Format::BC7Srgb:
    return {4, 4, 16};
  }
  return {1, 1, 0};
}

[[nodiscard]] constexpr bool isCompressedFormat(Format format) noexcept {
  return formatInfo(format).blockWidth > 1;
}

// Bytes of one uncompressed pixel; 0 for compressed formats, which have no per-pixel size.
[[nodiscard]] constexpr std::uint32_t bytesPerPixel(Format format) noexcept {
  return isCompressedFormat(format) ? 0 : formatInfo(format).bytesPerBlock;
}

// Bytes of one tightly packed mip level of `size` pixels.
[[nodiscard]] constexpr std::uint64_t levelByteSize(Format format, glm::uvec2 size) noexcept {
  const FormatInfo info = formatInfo(format);
  const std::uint64_t blocksX = (size.x + info.blockWidth - 1) / info.blockWidth;
  const std::uint64_t blocksY = (size.y + info.blockHeight - 1) / info.blockHeight;
  return blocksX * blocksY * info.bytesPerBlock;
}

// Levels in a full mip chain down to 1x1.
[[nodiscard]] constexpr std::uint32_t fullMipCount(glm::uvec2 size) noexcept {
  std::uint32_t levels = 1;
  for (std::uint32_t extent = std::max(size.x, size.y); extent > 1; extent /= 2) {
    ++levels;
  }
  return levels;
}

[[nodiscard]] constexpr glm::uvec2 mipSize(glm::uvec2 size, std::uint32_t level) noexcept {
  return {std::max(size.x >> level, 1u), std::max(size.y >> level, 1u)};
}

enum class BufferUsage : std::uint8_t {
  None = 0,
  TransferSrc = 1 << 0,
  TransferDst = 1 << 1,
  Uniform = 1 << 2,
  // Storage buffers also get a device address (IDevice::bufferAddress) for vertex pulling.
  Storage = 1 << 3,
  Index = 1 << 4,
  // Source of the draw commands and counts of drawIndexedIndirectCount (ADR-0012).
  Indirect = 1 << 5,
};

// One entry of the buffer drawIndexedIndirectCount reads, laid out as Vulkan's
// VkDrawIndexedIndirectCommand. `firstInstance` reaches the vertex shader as
// SV_StartInstanceLocation, which is where the engine puts the draw's object index (ADR-0012).
struct IndirectCommand {
  std::uint32_t indexCount{0};
  std::uint32_t instanceCount{0};
  std::uint32_t firstIndex{0};
  std::int32_t vertexOffset{0};
  std::uint32_t firstInstance{0};
};
static_assert(sizeof(IndirectCommand) == 20);

enum class ImageUsage : std::uint8_t {
  None = 0,
  TransferSrc = 1 << 0,
  TransferDst = 1 << 1,
  // Registered in the bindless sampled-image array (IDevice::sampledImageIndex).
  Sampled = 1 << 2,
  ColorAttachment = 1 << 3,
  DepthAttachment = 1 << 4,
  // Written by compute shaders through per-level storage views (IDevice::storageImageIndex).
  Storage = 1 << 5,
};

// Synchronization2 vocabulary, reduced to what the engine emits. Barriers name stages and
// accesses explicitly; the render graph derives them from how a pass uses a resource.
enum class PipelineStage : std::uint16_t {
  None = 0,
  VertexShader = 1 << 0,
  FragmentShader = 1 << 1,
  EarlyFragmentTests = 1 << 2,
  LateFragmentTests = 1 << 3,
  ColorAttachmentOutput = 1 << 4,
  ComputeShader = 1 << 5,
  Transfer = 1 << 6,
  AllGraphics = 1 << 7,
  AllCommands = 1 << 8,
  DrawIndirect = 1 << 9, // where an indirect draw fetches its commands and count
};

enum class Access : std::uint16_t {
  None = 0,
  ShaderRead = 1 << 0,
  ShaderWrite = 1 << 1,
  ColorAttachmentRead = 1 << 2,
  ColorAttachmentWrite = 1 << 3,
  DepthAttachmentRead = 1 << 4,
  DepthAttachmentWrite = 1 << 5,
  TransferRead = 1 << 6,
  TransferWrite = 1 << 7,
  MemoryRead = 1 << 8,
  MemoryWrite = 1 << 9,
  IndirectCommandRead = 1 << 10,
};

template <typename Flags>
concept FlagEnum = std::same_as<Flags, BufferUsage> || std::same_as<Flags, ImageUsage> ||
                   std::same_as<Flags, PipelineStage> || std::same_as<Flags, Access>;

template <FlagEnum Flags> constexpr Flags operator|(Flags a, Flags b) noexcept {
  using U = std::underlying_type_t<Flags>;
  return static_cast<Flags>(static_cast<U>(a) | static_cast<U>(b));
}
template <FlagEnum Flags> constexpr Flags operator&(Flags a, Flags b) noexcept {
  using U = std::underlying_type_t<Flags>;
  return static_cast<Flags>(static_cast<U>(a) & static_cast<U>(b));
}
template <FlagEnum Flags> constexpr Flags &operator|=(Flags &a, Flags b) noexcept {
  return a = a | b;
}
template <FlagEnum Flags> constexpr bool has(Flags set, Flags flag) noexcept {
  return (set & flag) == flag;
}
template <FlagEnum Flags> constexpr bool any(Flags set, Flags flags) noexcept {
  using U = std::underlying_type_t<Flags>;
  return (static_cast<U>(set) & static_cast<U>(flags)) != 0;
}

[[nodiscard]] constexpr bool isWrite(Access access) noexcept {
  return any(access, Access::ShaderWrite | Access::ColorAttachmentWrite | Access::DepthAttachmentWrite |
                         Access::TransferWrite | Access::MemoryWrite);
}

// Where the memory lives and who writes it. Host-visible buffers are persistently mapped.
enum class MemoryUsage : std::uint8_t {
  GpuOnly,
  CpuToGpu,
  GpuToCpu,
};

struct BufferDesc {
  std::uint64_t size{0};
  BufferUsage usage{BufferUsage::None};
  MemoryUsage memory{MemoryUsage::GpuOnly};
  std::string debugName;

  bool operator==(const BufferDesc &) const = default;
};

// A 2D image, optionally with a mip chain, or a cube map of six layers. Every barrier and
// upload addresses the whole image or one level and layer; there are no partial views.
struct ImageDesc {
  glm::uvec2 size{1, 1};
  Format format{Format::R8G8B8A8Unorm};
  ImageUsage usage{ImageUsage::None};
  std::uint32_t mipLevels{1};
  bool cube{false};
  std::string debugName;

  [[nodiscard]] constexpr std::uint32_t layers() const noexcept {
    return cube ? 6u : 1u;
  }
  [[nodiscard]] constexpr std::uint64_t byteSize() const noexcept {
    std::uint64_t bytes = 0;
    for (std::uint32_t level = 0; level < mipLevels; ++level) {
      bytes += levelByteSize(format, mipSize(size, level));
    }
    return bytes * layers();
  }

  bool operator==(const ImageDesc &) const = default;
};

// One level and layer of an image, tightly packed, for IDevice::uploadImage.
struct ImageUpload {
  std::uint32_t mipLevel{0};
  std::uint32_t layer{0};
  std::span<const std::byte> data{};
};

enum class Filter : std::uint8_t {
  Nearest,
  Linear,
};

enum class AddressMode : std::uint8_t {
  Repeat,
  ClampToEdge,
  MirroredRepeat,
};

// Samplers live in the bindless set: plain ones in the sampler array, ones with `compare` in the
// comparison-sampler array, each with its own index space (IDevice::samplerIndex).
struct SamplerDesc {
  Filter filter{Filter::Linear};
  Filter mipFilter{Filter::Linear};
  AddressMode addressMode{AddressMode::Repeat};
  float anisotropy{1.0f}; // 1 disables anisotropic filtering
  bool compare{false};    // depth comparison with CompareOp::GreaterOrEqual, for reversed-Z shadows
  std::string debugName;

  bool operator==(const SamplerDesc &) const = default;
};

enum class ImageLayout : std::uint8_t {
  Undefined,
  General,
  ColorAttachment,
  DepthAttachment,
  ShaderReadOnly,
  TransferSrc,
  TransferDst,
  Present,
};

// A global memory dependency, for buffers written by one pass and read by another.
struct MemoryBarrier {
  PipelineStage srcStage{PipelineStage::None};
  Access srcAccess{Access::None};
  PipelineStage dstStage{PipelineStage::None};
  Access dstAccess{Access::None};
};

// Whole-image transition with explicit synchronization on both sides. An Undefined old layout
// discards the contents; its stage still orders the transition after earlier work on the image.
struct ImageBarrier {
  ImageHandle image{};
  PipelineStage srcStage{PipelineStage::None};
  Access srcAccess{Access::None};
  ImageLayout oldLayout{ImageLayout::Undefined};
  PipelineStage dstStage{PipelineStage::None};
  Access dstAccess{Access::None};
  ImageLayout newLayout{ImageLayout::Undefined};
};

enum class LoadOp : std::uint8_t {
  Load,
  Clear,
  DontCare,
};

enum class StoreOp : std::uint8_t {
  Store,
  DontCare,
};

struct ColorAttachment {
  ImageHandle image{};
  LoadOp load{LoadOp::Clear};
  StoreOp store{StoreOp::Store};
  // Integer formats (isUintFormat) truncate each component to an integer clear value.
  glm::vec4 clearColor{0.0f, 0.0f, 0.0f, 1.0f};
};

// Reversed-Z: the clear value 0 is the far plane (docs/rendering.md, "Vulkan baseline").
struct DepthAttachment {
  ImageHandle image{};
  LoadOp load{LoadOp::Clear};
  StoreOp store{StoreOp::Store};
  float clearDepth{0.0f};
};

struct RenderingDesc {
  std::span<const ColorAttachment> colors{};
  const DepthAttachment *depth{nullptr};
};

enum class IndexType : std::uint8_t {
  Uint16,
  Uint32,
};

// One SPIR-V module holding every entry point of a .slang file, as slangc emits it with
// -fvk-use-entrypoint-name. The bytes are copied at creation.
struct ShaderDesc {
  std::span<const std::byte> spirv{};
  std::string debugName;
};

enum class CullMode : std::uint8_t {
  None,
  Back,
  Front,
};

// The winding a front face has on screen. Dynamic state on every graphics pipeline, reset to
// counter-clockwise when one is bound; ICommandList::setFrontFace flips it for a mirrored draw.
enum class FrontFace : std::uint8_t {
  CounterClockwise,
  Clockwise,
};

enum class CompareOp : std::uint8_t {
  Never,
  Less,
  Equal,
  LessOrEqual,
  Greater,
  GreaterOrEqual,
  Always,
};

// Reversed-Z defaults: nearer fragments have the greater depth.
struct DepthState {
  bool test{false};
  bool write{false};
  CompareOp compare{CompareOp::GreaterOrEqual};
};

enum class Topology : std::uint8_t {
  TriangleList,
  LineList, // debug drawing; lines are one pixel wide
};

enum class BlendMode : std::uint8_t {
  None,
  Alpha,    // source alpha, one minus source alpha
  Additive, // one, one
};

// Every pipeline, graphics or compute, shares one layout (docs/rendering.md, "Frame
// structure"). Set 0 is the bindless set, bound by bindPipeline: runtime arrays indexed by the
// values the device hands out for images and samplers. Set 1 holds the per-pass push
// descriptors below. Push constants of PushConstantSize bytes are visible to all stages.
// Viewport and scissor are always dynamic; front faces are counter-clockwise
// (docs/conventions.md).
constexpr std::uint32_t PushConstantSize = 128;
constexpr std::uint32_t BindlessDescriptorSet = 0;
constexpr std::uint32_t BindlessSampledImageBinding = 0;      // Texture2D[]
constexpr std::uint32_t BindlessSamplerBinding = 1;           // SamplerState[]
constexpr std::uint32_t BindlessStorageImageBinding = 2;      // RWTexture2DArray[], one view per mip level
constexpr std::uint32_t BindlessCubeImageBinding = 3;         // TextureCube[]
constexpr std::uint32_t BindlessComparisonSamplerBinding = 4; // SamplerComparisonState[]
constexpr std::uint32_t BindlessStorageBufferBinding = 5;     // StructuredBuffer<Vertex>[] (vertex pulling)
constexpr std::uint32_t BindlessDepthImageBinding = 6;        // Texture2D<float>[], depth images (ADR-0017)
constexpr std::uint32_t MaxBindlessSampledImages = 4096;
constexpr std::uint32_t MaxBindlessSamplers = 64;
constexpr std::uint32_t MaxBindlessStorageImages = 512;
constexpr std::uint32_t MaxBindlessCubeImages = 64;
constexpr std::uint32_t MaxBindlessComparisonSamplers = 8;
constexpr std::uint32_t MaxBindlessStorageBuffers = 4096;
constexpr std::uint32_t MaxBindlessDepthImages = 64;
// Index of a resource that is not in an array; shaders never read it.
constexpr std::uint32_t InvalidBindlessIndex = 0xFFFFFFFFu;
constexpr std::uint32_t PassDescriptorSet = 1;
constexpr std::uint32_t PassUniformBinding = 0; // a uniform buffer
constexpr std::uint32_t PassStorageBinding = 1; // a storage buffer
constexpr std::uint32_t PassImageBinding = 2;   // a sampled image read with Load, for post passes over one image

struct BufferBinding {
  std::uint32_t binding{PassUniformBinding};
  BufferHandle buffer{};
  std::uint64_t offset{0};
  std::uint64_t size{0}; // 0 binds to the end of the buffer
};

// A sampled-capable image in ImageLayout::ShaderReadOnly for the pass's fragment shader.
struct ImageBinding {
  std::uint32_t binding{PassImageBinding};
  ImageHandle image{};
};

struct GraphicsPipelineDesc {
  ShaderHandle shader{};
  std::string vertexEntry{"vertexMain"};
  std::string fragmentEntry{"fragmentMain"}; // empty for a depth-only pipeline without a fragment stage
  std::vector<Format> colorFormats;
  Format depthFormat{Format::Undefined};
  DepthState depth{};
  CullMode cullMode{CullMode::Back};
  BlendMode blend{BlendMode::None}; // applies to every colour attachment
  Topology topology{Topology::TriangleList};
  std::string debugName;
};

struct ComputePipelineDesc {
  ShaderHandle shader{};
  std::string entry{"computeMain"};
  std::string debugName;
};

// A slice of the frame's host-visible linear allocator, valid until the frame slot is reused.
struct TransientAllocation {
  BufferHandle buffer{};
  std::uint64_t offset{0};
  std::span<std::byte> data{};
};

// Timestamps a frame may write; results come back when the slot is reused (IDevice::timestamps).
constexpr std::uint32_t MaxTimestamps = 128;

struct HeapBudget {
  std::uint64_t usage{0};
  std::uint64_t budget{0};
  bool deviceLocal{false};
};

struct MemoryBudget {
  static constexpr std::uint32_t MaxHeaps = 16; // VK_MAX_MEMORY_HEAPS
  std::array<HeapBudget, MaxHeaps> heaps{};
  std::uint32_t heapCount{0};
};

} // namespace sonnet::rhi
