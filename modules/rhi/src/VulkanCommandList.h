#pragma once

#include <sonnet/rhi/CommandList.h>

#include <vulkan/vulkan_raii.hpp>

namespace sonnet::rhi {

class VulkanDevice;

class VulkanCommandList final : public ICommandList {
public:
  explicit VulkanCommandList(VulkanDevice &device) : m_device(device) {
  }

  void begin(const vk::raii::CommandBuffer &commandBuffer);
  void end();
  vk::CommandBuffer handle() const noexcept {
    return m_commandBuffer;
  }

  void barrier(std::span<const ImageBarrier> barriers) override;
  void memoryBarrier(const MemoryBarrier &barrier) override;
  void beginRendering(const RenderingDesc &desc) override;
  void endRendering() override;
  void bindPipeline(PipelineHandle pipeline) override;
  void setFrontFace(FrontFace frontFace) override;
  void bindBuffers(std::span<const BufferBinding> bindings) override;
  void bindImages(std::span<const ImageBinding> bindings) override;
  void pushConstants(std::span<const std::byte> data) override;
  void bindIndexBuffer(BufferHandle buffer, IndexType type) override;
  void draw(std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex,
            std::uint32_t firstInstance) override;
  void drawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex,
                   std::int32_t vertexOffset, std::uint32_t firstInstance) override;
  void drawIndexedIndirectCount(BufferHandle commands, std::uint64_t commandOffset, BufferHandle countBuffer,
                                std::uint64_t countOffset, std::uint32_t maxDrawCount) override;
  void drawIndexedIndirect(BufferHandle commands, std::uint64_t commandOffset, std::uint32_t drawCount) override;
  void dispatch(std::uint32_t groupsX, std::uint32_t groupsY, std::uint32_t groupsZ) override;
  void copyImageToBuffer(ImageHandle image, BufferHandle buffer) override;
  void writeTimestamp(std::uint32_t index) override;

private:
  void pushDescriptors(std::span<const vk::WriteDescriptorSet> writes);

  VulkanDevice &m_device;
  vk::CommandBuffer m_commandBuffer;
  const vk::raii::detail::DeviceDispatcher *m_dispatcher{nullptr};
  // The bind point of the last bound pipeline: push descriptors go to the same one.
  VkPipelineBindPoint m_bindPoint{VK_PIPELINE_BIND_POINT_GRAPHICS};
};

} // namespace sonnet::rhi
