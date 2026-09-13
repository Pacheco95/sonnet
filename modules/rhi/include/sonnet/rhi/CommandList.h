#pragma once

#include <sonnet/rhi/Types.h>

#include <cstddef>
#include <cstdint>
#include <span>

namespace sonnet::rhi {

// Records GPU work for the current frame. Obtained from IDevice::beginFrame, submitted by
// IDevice::endFrame. Nothing here throws or allocates on the hot path.
class ICommandList {
public:
  virtual ~ICommandList() = default;

  // Whole-image layout transitions with the stages and accesses the caller derived.
  virtual void barrier(std::span<const ImageBarrier> barriers) = 0;

  // Dynamic rendering into the attachments: colours in ImageLayout::ColorAttachment, depth in
  // ImageLayout::DepthAttachment. The render area is the first attachment's size; viewport and
  // scissor are set to cover it, with the negative-height viewport that flips Y so front faces
  // are counter-clockwise.
  virtual void beginRendering(const RenderingDesc &desc) = 0;
  virtual void endRendering() = 0;

  // Inside beginRendering/endRendering. The pipeline's attachment formats must match.
  virtual void bindPipeline(PipelineHandle pipeline) = 0;
  // Per-pass buffers pushed into PassDescriptorSet: PassUniformBinding takes a uniform buffer,
  // PassStorageBinding a storage buffer. Offsets follow the device's alignment rules; slices
  // from allocateTransient always do.
  virtual void bindBuffers(std::span<const BufferBinding> bindings) = 0;
  // At most PushConstantSize bytes, visible to every stage of the bound pipeline.
  virtual void pushConstants(std::span<const std::byte> data) = 0;
  virtual void bindIndexBuffer(BufferHandle buffer, IndexType type) = 0;
  virtual void draw(std::uint32_t vertexCount, std::uint32_t instanceCount = 1, std::uint32_t firstVertex = 0,
                    std::uint32_t firstInstance = 0) = 0;
  virtual void drawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount = 1, std::uint32_t firstIndex = 0,
                           std::int32_t vertexOffset = 0, std::uint32_t firstInstance = 0) = 0;

  // Image in TransferSrc, buffer with TransferDst usage and enough space for the tightly packed
  // pixels.
  virtual void copyImageToBuffer(ImageHandle image, BufferHandle buffer) = 0;

  // Writes the GPU clock once all earlier commands have completed, into slot `index` of the
  // frame's MaxTimestamps. Ignored on devices without timestamps.
  virtual void writeTimestamp(std::uint32_t index) = 0;
};

} // namespace sonnet::rhi
