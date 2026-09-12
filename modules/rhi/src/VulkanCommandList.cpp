#include "VulkanCommandList.h"

#include "VulkanDevice.h"
#include "VulkanTypes.h"

#include <sonnet/core/Assert.h>

#include <array>

namespace sonnet::rhi {

void VulkanCommandList::begin(const vk::raii::CommandBuffer &commandBuffer) {
  m_commandBuffer = *commandBuffer;
  m_dispatcher = commandBuffer.getDispatcher();
  const vk::CommandBufferBeginInfo info{vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
  m_dispatcher->vkBeginCommandBuffer(m_commandBuffer, reinterpret_cast<const VkCommandBufferBeginInfo *>(&info));
}

void VulkanCommandList::end() {
  m_dispatcher->vkEndCommandBuffer(m_commandBuffer);
}

void VulkanCommandList::barrier(ImageHandle image, ImageLayout from, ImageLayout to) {
  const VulkanImage *resource = m_device.findImage(image);
  SONNET_ASSERT(resource != nullptr, "barrier on a stale image handle {}:{}", image.index, image.generation);
  const LayoutSync src = syncFor(from, true);
  const LayoutSync dst = syncFor(to, false);
  const vk::ImageMemoryBarrier2 barrier{src.stage,
                                        src.access,
                                        dst.stage,
                                        dst.access,
                                        toVk(from),
                                        toVk(to),
                                        vk::QueueFamilyIgnored,
                                        vk::QueueFamilyIgnored,
                                        resource->image,
                                        vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
  const vk::DependencyInfo dependency{{}, {}, {}, barrier};
  m_dispatcher->vkCmdPipelineBarrier2(m_commandBuffer, reinterpret_cast<const VkDependencyInfo *>(&dependency));
}

void VulkanCommandList::beginRendering(std::span<const ColorAttachment> colors) {
  SONNET_ASSERT(!colors.empty(), "beginRendering needs at least one colour attachment");
  // Eight is the Vulkan minimum for maxColorAttachments; the engine never exceeds it.
  std::array<vk::RenderingAttachmentInfo, 8> attachments;
  SONNET_ASSERT(colors.size() <= attachments.size(), "too many colour attachments");
  glm::uvec2 extent{0, 0};
  for (std::size_t i = 0; i < colors.size(); ++i) {
    const VulkanImage *image = m_device.findImage(colors[i].image);
    SONNET_ASSERT(image != nullptr, "rendering to a stale image handle");
    if (i == 0) {
      extent = image->desc.size;
    }
    const glm::vec4 &c = colors[i].clearColor;
    attachments[i] = vk::RenderingAttachmentInfo{*image->view,
                                                 vk::ImageLayout::eColorAttachmentOptimal,
                                                 vk::ResolveModeFlagBits::eNone,
                                                 {},
                                                 vk::ImageLayout::eUndefined,
                                                 toVk(colors[i].load),
                                                 toVk(colors[i].store),
                                                 vk::ClearValue{vk::ClearColorValue{c.r, c.g, c.b, c.a}}};
  }
  const vk::Rect2D area{{0, 0}, {extent.x, extent.y}};
  const vk::RenderingInfo info{{}, area, 1, 0, static_cast<std::uint32_t>(colors.size()), attachments.data()};
  m_dispatcher->vkCmdBeginRendering(m_commandBuffer, reinterpret_cast<const VkRenderingInfo *>(&info));

  // Negative height flips clip-space Y so front faces stay counter-clockwise (docs/rendering.md).
  const vk::Viewport viewport{
      0.0f, static_cast<float>(extent.y), static_cast<float>(extent.x), -static_cast<float>(extent.y), 0.0f, 1.0f};
  m_dispatcher->vkCmdSetViewport(m_commandBuffer, 0, 1, reinterpret_cast<const VkViewport *>(&viewport));
  m_dispatcher->vkCmdSetScissor(m_commandBuffer, 0, 1, reinterpret_cast<const VkRect2D *>(&area));
}

void VulkanCommandList::endRendering() {
  m_dispatcher->vkCmdEndRendering(m_commandBuffer);
}

void VulkanCommandList::copyImageToBuffer(ImageHandle image, BufferHandle buffer) {
  const VulkanImage *src = m_device.findImage(image);
  const VulkanBuffer *dst = m_device.findBuffer(buffer);
  SONNET_ASSERT(src != nullptr && dst != nullptr, "copyImageToBuffer with a stale handle");
  SONNET_ASSERT(dst->desc.size >= std::uint64_t{src->desc.size.x} * src->desc.size.y * bytesPerPixel(src->desc.format),
                "readback buffer \"{}\" is too small for \"{}\"", dst->desc.debugName, src->desc.debugName);
  const vk::BufferImageCopy region{0,
                                   0,
                                   0,
                                   vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                                   vk::Offset3D{0, 0, 0},
                                   vk::Extent3D{src->desc.size.x, src->desc.size.y, 1}};
  m_dispatcher->vkCmdCopyImageToBuffer(m_commandBuffer, src->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, *dst->buffer,
                                       1, reinterpret_cast<const VkBufferImageCopy *>(&region));
}

} // namespace sonnet::rhi
