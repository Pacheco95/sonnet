#include "VulkanTypes.h"

namespace sonnet::rhi {

vk::Format toVk(Format format) noexcept {
  switch (format) {
  case Format::Undefined:
    return vk::Format::eUndefined;
  case Format::R8G8B8A8Unorm:
    return vk::Format::eR8G8B8A8Unorm;
  case Format::R8G8B8A8Srgb:
    return vk::Format::eR8G8B8A8Srgb;
  case Format::B8G8R8A8Unorm:
    return vk::Format::eB8G8R8A8Unorm;
  case Format::B8G8R8A8Srgb:
    return vk::Format::eB8G8R8A8Srgb;
  }
  return vk::Format::eUndefined;
}

Format fromVk(vk::Format format) noexcept {
  switch (format) {
  case vk::Format::eR8G8B8A8Unorm:
    return Format::R8G8B8A8Unorm;
  case vk::Format::eR8G8B8A8Srgb:
    return Format::R8G8B8A8Srgb;
  case vk::Format::eB8G8R8A8Unorm:
    return Format::B8G8R8A8Unorm;
  case vk::Format::eB8G8R8A8Srgb:
    return Format::B8G8R8A8Srgb;
  default:
    return Format::Undefined;
  }
}

vk::BufferUsageFlags toVk(BufferUsage usage) noexcept {
  vk::BufferUsageFlags flags;
  if (has(usage, BufferUsage::TransferSrc)) {
    flags |= vk::BufferUsageFlagBits::eTransferSrc;
  }
  if (has(usage, BufferUsage::TransferDst)) {
    flags |= vk::BufferUsageFlagBits::eTransferDst;
  }
  if (has(usage, BufferUsage::Uniform)) {
    flags |= vk::BufferUsageFlagBits::eUniformBuffer;
  }
  if (has(usage, BufferUsage::Storage)) {
    flags |= vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress;
  }
  return flags;
}

vk::ImageUsageFlags toVk(ImageUsage usage) noexcept {
  vk::ImageUsageFlags flags;
  if (has(usage, ImageUsage::TransferSrc)) {
    flags |= vk::ImageUsageFlagBits::eTransferSrc;
  }
  if (has(usage, ImageUsage::TransferDst)) {
    flags |= vk::ImageUsageFlagBits::eTransferDst;
  }
  if (has(usage, ImageUsage::Sampled)) {
    flags |= vk::ImageUsageFlagBits::eSampled;
  }
  if (has(usage, ImageUsage::ColorAttachment)) {
    flags |= vk::ImageUsageFlagBits::eColorAttachment;
  }
  return flags;
}

vk::ImageLayout toVk(ImageLayout layout) noexcept {
  switch (layout) {
  case ImageLayout::Undefined:
    return vk::ImageLayout::eUndefined;
  case ImageLayout::General:
    return vk::ImageLayout::eGeneral;
  case ImageLayout::ColorAttachment:
    return vk::ImageLayout::eColorAttachmentOptimal;
  case ImageLayout::ShaderReadOnly:
    return vk::ImageLayout::eShaderReadOnlyOptimal;
  case ImageLayout::TransferSrc:
    return vk::ImageLayout::eTransferSrcOptimal;
  case ImageLayout::TransferDst:
    return vk::ImageLayout::eTransferDstOptimal;
  case ImageLayout::Present:
    return vk::ImageLayout::ePresentSrcKHR;
  }
  return vk::ImageLayout::eUndefined;
}

vk::AttachmentLoadOp toVk(LoadOp op) noexcept {
  switch (op) {
  case LoadOp::Load:
    return vk::AttachmentLoadOp::eLoad;
  case LoadOp::Clear:
    return vk::AttachmentLoadOp::eClear;
  case LoadOp::DontCare:
    return vk::AttachmentLoadOp::eDontCare;
  }
  return vk::AttachmentLoadOp::eDontCare;
}

vk::AttachmentStoreOp toVk(StoreOp op) noexcept {
  return op == StoreOp::Store ? vk::AttachmentStoreOp::eStore : vk::AttachmentStoreOp::eDontCare;
}

LayoutSync syncFor(ImageLayout layout, bool asSource) noexcept {
  using Stage = vk::PipelineStageFlagBits2;
  using Access = vk::AccessFlagBits2;
  switch (layout) {
  case ImageLayout::Undefined:
    // As a source, wait for everything before: for a swapchain image that is the acquire
    // semaphore's stage, and synchronization validation checks that the transition is ordered
    // after it. Fresh images have nothing to wait for, so the cost is nil.
    return {asSource ? Stage::eAllCommands : Stage::eNone, Access::eNone};
  case ImageLayout::General:
    return {Stage::eAllCommands, Access::eMemoryRead | Access::eMemoryWrite};
  case ImageLayout::ColorAttachment:
    return {Stage::eColorAttachmentOutput, Access::eColorAttachmentRead | Access::eColorAttachmentWrite};
  case ImageLayout::ShaderReadOnly:
    return {Stage::eFragmentShader | Stage::eComputeShader, Access::eShaderSampledRead};
  case ImageLayout::TransferSrc:
    return {Stage::eCopy | Stage::eBlit, Access::eTransferRead};
  case ImageLayout::TransferDst:
    return {Stage::eCopy | Stage::eBlit | Stage::eClear, Access::eTransferWrite};
  case ImageLayout::Present:
    // Presentation is synchronised by the semaphore, not by memory accesses.
    return {asSource ? Stage::eColorAttachmentOutput : Stage::eBottomOfPipe, Access::eNone};
  }
  return {Stage::eAllCommands, Access::eMemoryRead | Access::eMemoryWrite};
}

} // namespace sonnet::rhi
