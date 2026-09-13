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
[[nodiscard]] vk::PipelineStageFlags2 toVk(PipelineStage stage) noexcept;
[[nodiscard]] vk::AccessFlags2 toVk(Access access) noexcept;
[[nodiscard]] vk::CompareOp toVk(CompareOp op) noexcept;
[[nodiscard]] vk::IndexType toVk(IndexType type) noexcept;
[[nodiscard]] vk::ImageAspectFlags aspectOf(Format format) noexcept;

} // namespace sonnet::rhi
