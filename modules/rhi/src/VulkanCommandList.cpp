#include "VulkanCommandList.h"

#include "VulkanDevice.h"
#include "VulkanTypes.h"

#include <sonnet/core/Assert.h>

#include <array>

namespace sonnet::rhi {

namespace {

// Eight is the Vulkan minimum for maxColorAttachments; the engine never exceeds it. Barriers
// and buffer bindings are bounded by what one pass declares.
constexpr std::size_t MaxColorAttachments = 8;
constexpr std::size_t MaxBarriers = 32;
constexpr std::size_t MaxBufferBindings = 2;
constexpr std::size_t MaxImageBindings = 1;

// Integer attachments read the integer members of the clear union; float ones the float members.
vk::ClearColorValue clearValueFor(Format format, const glm::vec4 &c) noexcept {
  if (isUintFormat(format)) {
    return vk::ClearColorValue{
        std::array<std::uint32_t, 4>{static_cast<std::uint32_t>(c.r), static_cast<std::uint32_t>(c.g),
                                     static_cast<std::uint32_t>(c.b), static_cast<std::uint32_t>(c.a)}};
  }
  return vk::ClearColorValue{c.r, c.g, c.b, c.a};
}

} // namespace

void VulkanCommandList::begin(const vk::raii::CommandBuffer &commandBuffer) {
  m_commandBuffer = *commandBuffer;
  m_dispatcher = commandBuffer.getDispatcher();
  m_bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  const vk::CommandBufferBeginInfo info{vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
  m_dispatcher->vkBeginCommandBuffer(m_commandBuffer, reinterpret_cast<const VkCommandBufferBeginInfo *>(&info));
}

void VulkanCommandList::end() {
  m_dispatcher->vkEndCommandBuffer(m_commandBuffer);
}

void VulkanCommandList::barrier(std::span<const ImageBarrier> barriers) {
  SONNET_ASSERT(barriers.size() <= MaxBarriers, "{} barriers in one call", barriers.size());
  std::array<vk::ImageMemoryBarrier2, MaxBarriers> vkBarriers;
  std::uint32_t count = 0;
  for (const ImageBarrier &barrier : barriers) {
    const VulkanImage *resource = m_device.findImage(barrier.image);
    SONNET_ASSERT(resource != nullptr, "barrier on a stale image handle {}:{}", barrier.image.index,
                  barrier.image.generation);
    vkBarriers[count++] = vk::ImageMemoryBarrier2{
        toVk(barrier.srcStage),  toVk(barrier.srcAccess),          toVk(barrier.dstStage), toVk(barrier.dstAccess),
        toVk(barrier.oldLayout), toVk(barrier.newLayout),          vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
        resource->image,         wholeImage(resource->desc.format)};
  }
  if (count == 0) {
    return;
  }
  const vk::DependencyInfo dependency{{}, 0, nullptr, 0, nullptr, count, vkBarriers.data()};
  m_dispatcher->vkCmdPipelineBarrier2(m_commandBuffer, reinterpret_cast<const VkDependencyInfo *>(&dependency));
}

void VulkanCommandList::memoryBarrier(const MemoryBarrier &barrier) {
  const vk::MemoryBarrier2 vkBarrier{toVk(barrier.srcStage), toVk(barrier.srcAccess), toVk(barrier.dstStage),
                                     toVk(barrier.dstAccess)};
  const vk::DependencyInfo dependency{{}, 1, &vkBarrier};
  m_dispatcher->vkCmdPipelineBarrier2(m_commandBuffer, reinterpret_cast<const VkDependencyInfo *>(&dependency));
}

void VulkanCommandList::beginRendering(const RenderingDesc &desc) {
  SONNET_ASSERT(!desc.colors.empty() || desc.depth != nullptr, "beginRendering needs an attachment");
  SONNET_ASSERT(desc.colors.size() <= MaxColorAttachments, "too many colour attachments");
  std::array<vk::RenderingAttachmentInfo, MaxColorAttachments> colors;
  glm::uvec2 extent{0, 0};
  for (std::size_t i = 0; i < desc.colors.size(); ++i) {
    const VulkanImage *image = m_device.findImage(desc.colors[i].image);
    SONNET_ASSERT(image != nullptr, "rendering to a stale image handle");
    if (i == 0) {
      extent = image->desc.size;
    }
    colors[i] =
        vk::RenderingAttachmentInfo{*image->view,
                                    vk::ImageLayout::eColorAttachmentOptimal,
                                    vk::ResolveModeFlagBits::eNone,
                                    {},
                                    vk::ImageLayout::eUndefined,
                                    toVk(desc.colors[i].load),
                                    toVk(desc.colors[i].store),
                                    vk::ClearValue{clearValueFor(image->desc.format, desc.colors[i].clearColor)}};
  }
  vk::RenderingAttachmentInfo depth;
  if (desc.depth != nullptr) {
    const VulkanImage *image = m_device.findImage(desc.depth->image);
    SONNET_ASSERT(image != nullptr, "rendering to a stale depth image handle");
    if (desc.colors.empty()) {
      extent = image->desc.size;
    }
    depth = vk::RenderingAttachmentInfo{*image->view,
                                        vk::ImageLayout::eDepthAttachmentOptimal,
                                        vk::ResolveModeFlagBits::eNone,
                                        {},
                                        vk::ImageLayout::eUndefined,
                                        toVk(desc.depth->load),
                                        toVk(desc.depth->store),
                                        vk::ClearValue{vk::ClearDepthStencilValue{desc.depth->clearDepth, 0}}};
  }
  const vk::Rect2D area{{0, 0}, {extent.x, extent.y}};
  const vk::RenderingInfo info{{},
                               area,
                               1,
                               0,
                               static_cast<std::uint32_t>(desc.colors.size()),
                               colors.data(),
                               desc.depth != nullptr ? &depth : nullptr};
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

void VulkanCommandList::bindPipeline(PipelineHandle pipeline) {
  const VulkanPipeline *resource = m_device.findPipeline(pipeline);
  SONNET_ASSERT(resource != nullptr, "binding a stale pipeline handle {}:{}", pipeline.index, pipeline.generation);
  m_bindPoint = resource->compute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;
  m_dispatcher->vkCmdBindPipeline(m_commandBuffer, m_bindPoint, *resource->pipeline);
  // Bound with every pipeline rather than once per frame: a foreign pipeline layout (Dear
  // ImGui's) may have replaced set 0 in between.
  const VkDescriptorSet set = m_device.bindlessSet();
  m_dispatcher->vkCmdBindDescriptorSets(m_commandBuffer, m_bindPoint, m_device.pipelineLayout(), BindlessDescriptorSet,
                                        1, &set, 0, nullptr);
}

void VulkanCommandList::pushDescriptors(std::span<const vk::WriteDescriptorSet> writes) {
  if (writes.empty()) {
    return;
  }
  m_dispatcher->vkCmdPushDescriptorSet(m_commandBuffer, m_bindPoint, m_device.pipelineLayout(), PassDescriptorSet,
                                       static_cast<std::uint32_t>(writes.size()),
                                       reinterpret_cast<const VkWriteDescriptorSet *>(writes.data()));
}

void VulkanCommandList::bindBuffers(std::span<const BufferBinding> bindings) {
  SONNET_ASSERT(bindings.size() <= MaxBufferBindings, "{} buffer bindings in one call", bindings.size());
  std::array<vk::DescriptorBufferInfo, MaxBufferBindings> infos;
  std::array<vk::WriteDescriptorSet, MaxBufferBindings> writes;
  std::uint32_t count = 0;
  for (const BufferBinding &binding : bindings) {
    const VulkanBuffer *buffer = m_device.findBuffer(binding.buffer);
    SONNET_ASSERT(buffer != nullptr, "binding a stale buffer handle {}:{}", binding.buffer.index,
                  binding.buffer.generation);
    SONNET_ASSERT(binding.binding == PassUniformBinding || binding.binding == PassStorageBinding,
                  "binding {} is not in the pass set layout", binding.binding);
    const vk::DescriptorType type =
        binding.binding == PassUniformBinding ? vk::DescriptorType::eUniformBuffer : vk::DescriptorType::eStorageBuffer;
    infos[count] =
        vk::DescriptorBufferInfo{*buffer->buffer, binding.offset, binding.size != 0 ? binding.size : vk::WholeSize};
    writes[count] = vk::WriteDescriptorSet{{}, binding.binding, 0, 1, type, nullptr, &infos[count]};
    ++count;
  }
  pushDescriptors({writes.data(), count});
}

void VulkanCommandList::bindImages(std::span<const ImageBinding> bindings) {
  SONNET_ASSERT(bindings.size() <= MaxImageBindings, "{} image bindings in one call", bindings.size());
  std::array<vk::DescriptorImageInfo, MaxImageBindings> infos;
  std::array<vk::WriteDescriptorSet, MaxImageBindings> writes;
  std::uint32_t count = 0;
  for (const ImageBinding &binding : bindings) {
    const VulkanImage *image = m_device.findImage(binding.image);
    SONNET_ASSERT(image != nullptr, "binding a stale image handle {}:{}", binding.image.index,
                  binding.image.generation);
    SONNET_ASSERT(binding.binding == PassImageBinding, "binding {} is not an image binding of the pass set",
                  binding.binding);
    infos[count] = vk::DescriptorImageInfo{nullptr, *image->view, vk::ImageLayout::eShaderReadOnlyOptimal};
    writes[count] = vk::WriteDescriptorSet{{}, binding.binding, 0, 1, vk::DescriptorType::eSampledImage, &infos[count]};
    ++count;
  }
  pushDescriptors({writes.data(), count});
}

void VulkanCommandList::pushConstants(std::span<const std::byte> data) {
  SONNET_ASSERT(data.size() <= PushConstantSize && data.size() % 4 == 0, "push constants: {} bytes", data.size());
  m_dispatcher->vkCmdPushConstants(m_commandBuffer, m_device.pipelineLayout(), VK_SHADER_STAGE_ALL, 0,
                                   static_cast<std::uint32_t>(data.size()), data.data());
}

void VulkanCommandList::bindIndexBuffer(BufferHandle buffer, IndexType type) {
  const VulkanBuffer *resource = m_device.findBuffer(buffer);
  SONNET_ASSERT(resource != nullptr, "binding a stale index buffer handle {}:{}", buffer.index, buffer.generation);
  m_dispatcher->vkCmdBindIndexBuffer(m_commandBuffer, *resource->buffer, 0, static_cast<VkIndexType>(toVk(type)));
}

void VulkanCommandList::draw(std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex,
                             std::uint32_t firstInstance) {
  m_dispatcher->vkCmdDraw(m_commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
}

void VulkanCommandList::drawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex,
                                    std::int32_t vertexOffset, std::uint32_t firstInstance) {
  m_dispatcher->vkCmdDrawIndexed(m_commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void VulkanCommandList::dispatch(std::uint32_t groupsX, std::uint32_t groupsY, std::uint32_t groupsZ) {
  SONNET_ASSERT(m_bindPoint == VK_PIPELINE_BIND_POINT_COMPUTE, "dispatch without a compute pipeline");
  m_dispatcher->vkCmdDispatch(m_commandBuffer, groupsX, groupsY, groupsZ);
}

void VulkanCommandList::copyImageToBuffer(ImageHandle image, BufferHandle buffer) {
  const VulkanImage *src = m_device.findImage(image);
  const VulkanBuffer *dst = m_device.findBuffer(buffer);
  SONNET_ASSERT(src != nullptr && dst != nullptr, "copyImageToBuffer with a stale handle");
  SONNET_ASSERT(dst->desc.size >= levelByteSize(src->desc.format, src->desc.size),
                "readback buffer \"{}\" is too small for \"{}\"", dst->desc.debugName, src->desc.debugName);
  const vk::BufferImageCopy region{0,
                                   0,
                                   0,
                                   vk::ImageSubresourceLayers{aspectOf(src->desc.format), 0, 0, 1},
                                   vk::Offset3D{0, 0, 0},
                                   vk::Extent3D{src->desc.size.x, src->desc.size.y, 1}};
  m_dispatcher->vkCmdCopyImageToBuffer(m_commandBuffer, src->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, *dst->buffer,
                                       1, reinterpret_cast<const VkBufferImageCopy *>(&region));
}

void VulkanCommandList::writeTimestamp(std::uint32_t index) {
  SONNET_ASSERT(index < MaxTimestamps, "timestamp index {} out of range", index);
  const vk::QueryPool pool = m_device.currentQueryPool();
  if (pool == nullptr) {
    return;
  }
  m_device.noteTimestamp(index);
  m_dispatcher->vkCmdWriteTimestamp2(m_commandBuffer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, pool, index);
}

} // namespace sonnet::rhi
