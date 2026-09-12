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

  void barrier(ImageHandle image, ImageLayout from, ImageLayout to) override;
  void beginRendering(std::span<const ColorAttachment> colors) override;
  void endRendering() override;
  void copyImageToBuffer(ImageHandle image, BufferHandle buffer) override;

private:
  VulkanDevice &m_device;
  vk::CommandBuffer m_commandBuffer;
  const vk::raii::detail::DeviceDispatcher *m_dispatcher{nullptr};
};

} // namespace sonnet::rhi
