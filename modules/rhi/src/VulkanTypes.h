#pragma once

#include <sonnet/rhi/Types.h>

#include <vulkan/vulkan_raii.hpp>

namespace sonnet::rhi {

[[nodiscard]] vk::Format toVk(Format format) noexcept;
// Undefined for formats the engine has no name for.
[[nodiscard]] Format fromVk(vk::Format format) noexcept;
[[nodiscard]] vk::BufferUsageFlags toVk(BufferUsage usage) noexcept;
[[nodiscard]] vk::ImageUsageFlags toVk(ImageUsage usage) noexcept;
[[nodiscard]] vk::ImageLayout toVk(ImageLayout layout) noexcept;
[[nodiscard]] vk::AttachmentLoadOp toVk(LoadOp op) noexcept;
[[nodiscard]] vk::AttachmentStoreOp toVk(StoreOp op) noexcept;

struct LayoutSync {
  vk::PipelineStageFlags2 stage;
  vk::AccessFlags2 access;
};
// The stages and accesses a layout implies as the source or destination of a barrier.
[[nodiscard]] LayoutSync syncFor(ImageLayout layout, bool asSource) noexcept;

} // namespace sonnet::rhi
