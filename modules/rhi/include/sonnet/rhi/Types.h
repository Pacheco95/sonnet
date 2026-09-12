#pragma once

#include <sonnet/core/Handle.h>
#include <sonnet/core/Math.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace sonnet::rhi {

struct BufferTag {};
struct ImageTag {};
using BufferHandle = core::Handle<BufferTag>;
using ImageHandle = core::Handle<ImageTag>;

enum class Format : std::uint8_t {
  Undefined,
  R8G8B8A8Unorm,
  R8G8B8A8Srgb,
  B8G8R8A8Unorm,
  B8G8R8A8Srgb,
};

[[nodiscard]] constexpr std::uint32_t bytesPerPixel(Format format) noexcept {
  switch (format) {
  case Format::Undefined:
    return 0;
  case Format::R8G8B8A8Unorm:
  case Format::R8G8B8A8Srgb:
  case Format::B8G8R8A8Unorm:
  case Format::B8G8R8A8Srgb:
    return 4;
  }
  return 0;
}

enum class BufferUsage : std::uint8_t {
  None = 0,
  TransferSrc = 1 << 0,
  TransferDst = 1 << 1,
  Uniform = 1 << 2,
  Storage = 1 << 3,
};

enum class ImageUsage : std::uint8_t {
  None = 0,
  TransferSrc = 1 << 0,
  TransferDst = 1 << 1,
  Sampled = 1 << 2,
  ColorAttachment = 1 << 3,
};

template <typename Flags>
concept FlagEnum = std::same_as<Flags, BufferUsage> || std::same_as<Flags, ImageUsage>;

template <FlagEnum Flags> constexpr Flags operator|(Flags a, Flags b) noexcept {
  return static_cast<Flags>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}
template <FlagEnum Flags> constexpr Flags operator&(Flags a, Flags b) noexcept {
  return static_cast<Flags>(static_cast<std::uint8_t>(a) & static_cast<std::uint8_t>(b));
}
template <FlagEnum Flags> constexpr bool has(Flags set, Flags flag) noexcept {
  return (set & flag) == flag;
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
};

struct ImageDesc {
  glm::uvec2 size{1, 1};
  Format format{Format::R8G8B8A8Unorm};
  ImageUsage usage{ImageUsage::None};
  std::string debugName;
};

// Layouts double as the synchronization vocabulary for now: a transition between two layouts
// implies the stages and accesses of both sides. The render graph refines this in M1.
enum class ImageLayout : std::uint8_t {
  Undefined,
  General,
  ColorAttachment,
  ShaderReadOnly,
  TransferSrc,
  TransferDst,
  Present,
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
  ImageHandle image;
  LoadOp load{LoadOp::Clear};
  StoreOp store{StoreOp::Store};
  glm::vec4 clearColor{0.0f, 0.0f, 0.0f, 1.0f};
};

} // namespace sonnet::rhi
