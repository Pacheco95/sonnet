#pragma once

#include <sonnet/core/Handle.h>
#include <sonnet/core/Math.h>

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
struct ShaderTag {};
struct PipelineTag {};
using BufferHandle = core::Handle<BufferTag>;
using ImageHandle = core::Handle<ImageTag>;
using ShaderHandle = core::Handle<ShaderTag>;
using PipelineHandle = core::Handle<PipelineTag>;

enum class Format : std::uint8_t {
  Undefined,
  R8G8B8A8Unorm,
  R8G8B8A8Srgb,
  B8G8R8A8Unorm,
  B8G8R8A8Srgb,
  D32Sfloat,
};

[[nodiscard]] constexpr bool isDepthFormat(Format format) noexcept {
  return format == Format::D32Sfloat;
}

[[nodiscard]] constexpr std::uint32_t bytesPerPixel(Format format) noexcept {
  switch (format) {
  case Format::Undefined:
    return 0;
  case Format::R8G8B8A8Unorm:
  case Format::R8G8B8A8Srgb:
  case Format::B8G8R8A8Unorm:
  case Format::B8G8R8A8Srgb:
  case Format::D32Sfloat:
    return 4;
  }
  return 0;
}

enum class BufferUsage : std::uint8_t {
  None = 0,
  TransferSrc = 1 << 0,
  TransferDst = 1 << 1,
  Uniform = 1 << 2,
  // Storage buffers also get a device address (IDevice::bufferAddress) for vertex pulling.
  Storage = 1 << 3,
  Index = 1 << 4,
};

enum class ImageUsage : std::uint8_t {
  None = 0,
  TransferSrc = 1 << 0,
  TransferDst = 1 << 1,
  Sampled = 1 << 2,
  ColorAttachment = 1 << 3,
  DepthAttachment = 1 << 4,
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

struct ImageDesc {
  glm::uvec2 size{1, 1};
  Format format{Format::R8G8B8A8Unorm};
  ImageUsage usage{ImageUsage::None};
  std::string debugName;

  bool operator==(const ImageDesc &) const = default;
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

// Every graphics pipeline shares one layout (docs/rendering.md, "Frame structure"): set 0 is
// the bindless set, empty until textures arrive; set 1 holds the per-pass push descriptors
// below; push constants of PushConstantSize bytes are visible to all stages. Viewport and
// scissor are always dynamic; front faces are counter-clockwise (docs/conventions.md).
constexpr std::uint32_t PushConstantSize = 128;
constexpr std::uint32_t PassDescriptorSet = 1;
constexpr std::uint32_t PassUniformBinding = 0; // a uniform buffer
constexpr std::uint32_t PassStorageBinding = 1; // a storage buffer

struct BufferBinding {
  std::uint32_t binding{PassUniformBinding};
  BufferHandle buffer{};
  std::uint64_t offset{0};
  std::uint64_t size{0}; // 0 binds to the end of the buffer
};

struct GraphicsPipelineDesc {
  ShaderHandle shader{};
  std::string vertexEntry{"vertexMain"};
  std::string fragmentEntry{"fragmentMain"};
  std::vector<Format> colorFormats;
  Format depthFormat{Format::Undefined};
  DepthState depth{};
  CullMode cullMode{CullMode::Back};
  std::string debugName;
};

// A slice of the frame's host-visible linear allocator, valid until the frame slot is reused.
struct TransientAllocation {
  BufferHandle buffer{};
  std::uint64_t offset{0};
  std::span<std::byte> data{};
};

// Timestamps a frame may write; results come back when the slot is reused (IDevice::timestamps).
constexpr std::uint32_t MaxTimestamps = 64;

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
