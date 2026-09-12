#pragma once

#include <sonnet/rhi/Types.h>

#include <vulkan-memory-allocator-hpp/vk_mem_alloc.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <cstddef>

namespace sonnet::rhi {

// Members are declared so that the Vulkan object is destroyed before its memory (ADR-0006).

struct VulkanBuffer {
  BufferDesc desc;
  vma::UniqueAllocation allocation;
  vma::UniqueBuffer buffer;
  std::byte *mapped{nullptr};
};

struct VulkanImage {
  ImageDesc desc;
  // Empty for swapchain images, which the swapchain owns.
  vma::UniqueAllocation allocation;
  vma::UniqueImage ownedImage;
  vk::Image image;
  vk::raii::ImageView view{nullptr};
};

} // namespace sonnet::rhi
