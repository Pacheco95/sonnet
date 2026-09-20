#include "VulkanTypes.h"

namespace sonnet::rhi {

vk::Format toVk(Format format) noexcept {
  switch (format) {
  case Format::Undefined:
    return vk::Format::eUndefined;
  case Format::R8Unorm:
    return vk::Format::eR8Unorm;
  case Format::R8G8B8A8Unorm:
    return vk::Format::eR8G8B8A8Unorm;
  case Format::R8G8B8A8Srgb:
    return vk::Format::eR8G8B8A8Srgb;
  case Format::B8G8R8A8Unorm:
    return vk::Format::eB8G8R8A8Unorm;
  case Format::B8G8R8A8Srgb:
    return vk::Format::eB8G8R8A8Srgb;
  case Format::R16G16Sfloat:
    return vk::Format::eR16G16Sfloat;
  case Format::R16G16B16A16Sfloat:
    return vk::Format::eR16G16B16A16Sfloat;
  case Format::R32Uint:
    return vk::Format::eR32Uint;
  case Format::BC4Unorm:
    return vk::Format::eBc4UnormBlock;
  case Format::BC5Unorm:
    return vk::Format::eBc5UnormBlock;
  case Format::BC7Unorm:
    return vk::Format::eBc7UnormBlock;
  case Format::BC7Srgb:
    return vk::Format::eBc7SrgbBlock;
  case Format::D32Sfloat:
    return vk::Format::eD32Sfloat;
  }
  return vk::Format::eUndefined;
}

Format fromVk(vk::Format format) noexcept {
  switch (format) {
  case vk::Format::eR8Unorm:
    return Format::R8Unorm;
  case vk::Format::eR8G8B8A8Unorm:
    return Format::R8G8B8A8Unorm;
  case vk::Format::eR8G8B8A8Srgb:
    return Format::R8G8B8A8Srgb;
  case vk::Format::eB8G8R8A8Unorm:
    return Format::B8G8R8A8Unorm;
  case vk::Format::eB8G8R8A8Srgb:
    return Format::B8G8R8A8Srgb;
  case vk::Format::eR16G16Sfloat:
    return Format::R16G16Sfloat;
  case vk::Format::eR16G16B16A16Sfloat:
    return Format::R16G16B16A16Sfloat;
  case vk::Format::eR32Uint:
    return Format::R32Uint;
  case vk::Format::eBc4UnormBlock:
    return Format::BC4Unorm;
  case vk::Format::eBc5UnormBlock:
    return Format::BC5Unorm;
  case vk::Format::eBc7UnormBlock:
    return Format::BC7Unorm;
  case vk::Format::eBc7SrgbBlock:
    return Format::BC7Srgb;
  case vk::Format::eD32Sfloat:
    return Format::D32Sfloat;
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
  if (has(usage, BufferUsage::Index)) {
    flags |= vk::BufferUsageFlagBits::eIndexBuffer;
  }
  if (has(usage, BufferUsage::Indirect)) {
    flags |= vk::BufferUsageFlagBits::eIndirectBuffer;
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
  if (has(usage, ImageUsage::DepthAttachment)) {
    flags |= vk::ImageUsageFlagBits::eDepthStencilAttachment;
  }
  if (has(usage, ImageUsage::Storage)) {
    flags |= vk::ImageUsageFlagBits::eStorage;
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
  case ImageLayout::DepthAttachment:
    return vk::ImageLayout::eDepthAttachmentOptimal;
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

vk::PipelineStageFlags2 toVk(PipelineStage stage) noexcept {
  using Bits = vk::PipelineStageFlagBits2;
  vk::PipelineStageFlags2 flags;
  if (has(stage, PipelineStage::VertexShader)) {
    flags |= Bits::eVertexShader;
  }
  if (has(stage, PipelineStage::FragmentShader)) {
    flags |= Bits::eFragmentShader;
  }
  if (has(stage, PipelineStage::EarlyFragmentTests)) {
    flags |= Bits::eEarlyFragmentTests;
  }
  if (has(stage, PipelineStage::LateFragmentTests)) {
    flags |= Bits::eLateFragmentTests;
  }
  if (has(stage, PipelineStage::ColorAttachmentOutput)) {
    flags |= Bits::eColorAttachmentOutput;
  }
  if (has(stage, PipelineStage::ComputeShader)) {
    flags |= Bits::eComputeShader;
  }
  if (has(stage, PipelineStage::Transfer)) {
    flags |= Bits::eAllTransfer;
  }
  if (has(stage, PipelineStage::AllGraphics)) {
    flags |= Bits::eAllGraphics;
  }
  if (has(stage, PipelineStage::AllCommands)) {
    flags |= Bits::eAllCommands;
  }
  if (has(stage, PipelineStage::DrawIndirect)) {
    flags |= Bits::eDrawIndirect;
  }
  return flags;
}

vk::AccessFlags2 toVk(Access access) noexcept {
  using Bits = vk::AccessFlagBits2;
  vk::AccessFlags2 flags;
  if (has(access, Access::ShaderRead)) {
    flags |= Bits::eShaderRead;
  }
  if (has(access, Access::ShaderWrite)) {
    flags |= Bits::eShaderWrite;
  }
  if (has(access, Access::ColorAttachmentRead)) {
    flags |= Bits::eColorAttachmentRead;
  }
  if (has(access, Access::ColorAttachmentWrite)) {
    flags |= Bits::eColorAttachmentWrite;
  }
  if (has(access, Access::DepthAttachmentRead)) {
    flags |= Bits::eDepthStencilAttachmentRead;
  }
  if (has(access, Access::DepthAttachmentWrite)) {
    flags |= Bits::eDepthStencilAttachmentWrite;
  }
  if (has(access, Access::TransferRead)) {
    flags |= Bits::eTransferRead;
  }
  if (has(access, Access::TransferWrite)) {
    flags |= Bits::eTransferWrite;
  }
  if (has(access, Access::MemoryRead)) {
    flags |= Bits::eMemoryRead;
  }
  if (has(access, Access::MemoryWrite)) {
    flags |= Bits::eMemoryWrite;
  }
  if (has(access, Access::IndirectCommandRead)) {
    flags |= Bits::eIndirectCommandRead;
  }
  return flags;
}

vk::CompareOp toVk(CompareOp op) noexcept {
  switch (op) {
  case CompareOp::Never:
    return vk::CompareOp::eNever;
  case CompareOp::Less:
    return vk::CompareOp::eLess;
  case CompareOp::Equal:
    return vk::CompareOp::eEqual;
  case CompareOp::LessOrEqual:
    return vk::CompareOp::eLessOrEqual;
  case CompareOp::Greater:
    return vk::CompareOp::eGreater;
  case CompareOp::GreaterOrEqual:
    return vk::CompareOp::eGreaterOrEqual;
  case CompareOp::Always:
    return vk::CompareOp::eAlways;
  }
  return vk::CompareOp::eAlways;
}

vk::IndexType toVk(IndexType type) noexcept {
  return type == IndexType::Uint16 ? vk::IndexType::eUint16 : vk::IndexType::eUint32;
}

vk::Filter toVk(Filter filter) noexcept {
  return filter == Filter::Nearest ? vk::Filter::eNearest : vk::Filter::eLinear;
}

vk::SamplerMipmapMode toVkMipmapMode(Filter filter) noexcept {
  return filter == Filter::Nearest ? vk::SamplerMipmapMode::eNearest : vk::SamplerMipmapMode::eLinear;
}

vk::SamplerAddressMode toVk(AddressMode mode) noexcept {
  switch (mode) {
  case AddressMode::Repeat:
    return vk::SamplerAddressMode::eRepeat;
  case AddressMode::ClampToEdge:
    return vk::SamplerAddressMode::eClampToEdge;
  case AddressMode::MirroredRepeat:
    return vk::SamplerAddressMode::eMirroredRepeat;
  }
  return vk::SamplerAddressMode::eRepeat;
}

vk::PipelineColorBlendAttachmentState toVk(BlendMode mode) noexcept {
  vk::PipelineColorBlendAttachmentState state;
  state.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                         vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
  switch (mode) {
  case BlendMode::None:
    state.blendEnable = VK_FALSE;
    break;
  case BlendMode::Alpha:
    state.blendEnable = VK_TRUE;
    state.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
    state.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
    state.colorBlendOp = vk::BlendOp::eAdd;
    state.srcAlphaBlendFactor = vk::BlendFactor::eOne;
    state.dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
    state.alphaBlendOp = vk::BlendOp::eAdd;
    break;
  case BlendMode::Additive:
    state.blendEnable = VK_TRUE;
    state.srcColorBlendFactor = vk::BlendFactor::eOne;
    state.dstColorBlendFactor = vk::BlendFactor::eOne;
    state.colorBlendOp = vk::BlendOp::eAdd;
    state.srcAlphaBlendFactor = vk::BlendFactor::eOne;
    state.dstAlphaBlendFactor = vk::BlendFactor::eOne;
    state.alphaBlendOp = vk::BlendOp::eAdd;
    break;
  }
  return state;
}

vk::ImageAspectFlags aspectOf(Format format) noexcept {
  return isDepthFormat(format) ? vk::ImageAspectFlagBits::eDepth : vk::ImageAspectFlagBits::eColor;
}

vk::ImageSubresourceRange wholeImage(Format format) noexcept {
  return vk::ImageSubresourceRange{aspectOf(format), 0, vk::RemainingMipLevels, 0, vk::RemainingArrayLayers};
}

} // namespace sonnet::rhi
