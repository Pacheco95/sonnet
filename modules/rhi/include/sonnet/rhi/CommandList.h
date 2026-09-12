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

  // Whole-image layout transition, with the stages and accesses implied by the two layouts.
  virtual void barrier(ImageHandle image, ImageLayout from, ImageLayout to) = 0;

  // Dynamic rendering into the attachments, which must be in ImageLayout::ColorAttachment. The
  // render area is the first attachment's size; viewport and scissor are set to cover it, with
  // the negative-height viewport that flips Y so front faces are counter-clockwise.
  virtual void beginRendering(std::span<const ColorAttachment> colors) = 0;
  virtual void endRendering() = 0;

  // Inside beginRendering/endRendering. The pipeline's colour formats must match the attachments.
  virtual void bindPipeline(PipelineHandle pipeline) = 0;
  // At most PushConstantSize bytes, visible to every stage of the bound pipeline.
  virtual void pushConstants(std::span<const std::byte> data) = 0;
  virtual void draw(std::uint32_t vertexCount, std::uint32_t instanceCount = 1, std::uint32_t firstVertex = 0,
                    std::uint32_t firstInstance = 0) = 0;

  // Image in TransferSrc, buffer with TransferDst usage and enough space for the tightly packed
  // pixels.
  virtual void copyImageToBuffer(ImageHandle image, BufferHandle buffer) = 0;
};

} // namespace sonnet::rhi
