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
  // A global memory dependency, for a buffer one pass writes and a later pass reads.
  virtual void memoryBarrier(const MemoryBarrier &barrier) = 0;

  // Dynamic rendering into the attachments: colours in ImageLayout::ColorAttachment, depth in
  // ImageLayout::DepthAttachment. The render area is the first attachment's size; viewport and
  // scissor are set to cover it, with the negative-height viewport that flips Y so front faces
  // are counter-clockwise.
  virtual void beginRendering(const RenderingDesc &desc) = 0;
  virtual void endRendering() = 0;

  // A graphics pipeline inside beginRendering/endRendering, with attachment formats that match;
  // a compute pipeline outside. Binds the bindless set for the pipeline's bind point too, and
  // for a graphics pipeline sets the front face back to counter-clockwise.
  virtual void bindPipeline(PipelineHandle pipeline) = 0;
  // With a graphics pipeline bound, until the next bind: which winding faces the viewer. A
  // transform that mirrors reverses a triangle's winding on screen, so its draws set Clockwise.
  virtual void setFrontFace(FrontFace frontFace) = 0;
  // Per-pass buffers pushed into PassDescriptorSet: PassUniformBinding takes a uniform buffer,
  // PassStorageBinding a storage buffer. Offsets follow the device's alignment rules; slices
  // from allocateTransient always do.
  virtual void bindBuffers(std::span<const BufferBinding> bindings) = 0;
  // Per-pass images pushed into PassDescriptorSet at PassImageBinding. The image must be in
  // ImageLayout::ShaderReadOnly, which the render graph arranges for a sampled use.
  virtual void bindImages(std::span<const ImageBinding> bindings) = 0;
  // At most PushConstantSize bytes, visible to every stage of the bound pipeline.
  virtual void pushConstants(std::span<const std::byte> data) = 0;
  virtual void bindIndexBuffer(BufferHandle buffer, IndexType type) = 0;
  virtual void draw(std::uint32_t vertexCount, std::uint32_t instanceCount = 1, std::uint32_t firstVertex = 0,
                    std::uint32_t firstInstance = 0) = 0;
  virtual void drawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount = 1, std::uint32_t firstIndex = 0,
                           std::int32_t vertexOffset = 0, std::uint32_t firstInstance = 0) = 0;
  // `drawCount` IndirectCommands from `commands` at `commandOffset`. The buffer needs
  // BufferUsage::Indirect, and a barrier into PipelineStage::DrawIndirect with
  // Access::IndirectCommandRead has to order whatever wrote it before this. The index buffer
  // bound applies to every command; an indirect draw cannot change it. A command that must draw
  // nothing carries an instance count of zero (ADR-0016).
  virtual void drawIndexedIndirect(BufferHandle commands, std::uint64_t commandOffset, std::uint32_t drawCount) = 0;

  // Outside beginRendering/endRendering, with a compute pipeline bound: workgroup counts.
  virtual void dispatch(std::uint32_t groupsX, std::uint32_t groupsY = 1, std::uint32_t groupsZ = 1) = 0;

  // The first level and layer of an image in TransferSrc into a buffer with TransferDst usage and
  // enough space for the tightly packed pixels.
  virtual void copyImageToBuffer(ImageHandle image, BufferHandle buffer) = 0;

  // Writes the GPU clock once all earlier commands have completed, into slot `index` of the
  // frame's MaxTimestamps. Ignored on devices without timestamps.
  virtual void writeTimestamp(std::uint32_t index) = 0;
};

} // namespace sonnet::rhi
