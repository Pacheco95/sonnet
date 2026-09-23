#pragma once

#include <sonnet/rhi/Swapchain.h>

#include <sonnet/platform/Window.h>

#include <vulkan/vulkan_raii.hpp>

#include <cstdint>
#include <vector>

namespace sonnet::rhi {

class VulkanDevice;

class VulkanSwapchain final : public ISwapchain {
public:
  VulkanSwapchain(VulkanDevice &device, platform::IWindow &window);
  ~VulkanSwapchain() override;
  VulkanSwapchain(const VulkanSwapchain &) = delete;
  VulkanSwapchain &operator=(const VulkanSwapchain &) = delete;

  std::optional<SwapchainImage> acquire() override;
  void requestResize() override;
  Format format() const override {
    return m_format;
  }
  glm::uvec2 extent() const override {
    return m_extent;
  }
  std::uint32_t imageCount() const override {
    return static_cast<std::uint32_t>(m_images.size());
  }
  bool readable() const override {
    return m_readable;
  }

  vk::SwapchainKHR handle() const noexcept {
    return *m_swapchain;
  }
  vk::Semaphore renderFinished(std::uint32_t imageIndex) const noexcept {
    return *m_renderFinished[imageIndex];
  }
  // Called by the device after a present that reported out of date or suboptimal.
  void markOutOfDate() noexcept {
    m_needsRecreate = true;
  }

private:
  // Returns false when the window has no drawable area.
  bool create(vk::SwapchainKHR oldSwapchain);
  void releaseImages();

  VulkanDevice &m_device;
  platform::IWindow &m_window;
  // Surface outlives the swapchain: declared first, destroyed last.
  vk::raii::SurfaceKHR m_surface{nullptr};
  vk::raii::SwapchainKHR m_swapchain{nullptr};
  std::vector<ImageHandle> m_images;
  std::vector<vk::raii::Semaphore> m_renderFinished;
  Format m_format{Format::Undefined};
  glm::uvec2 m_extent{0, 0};
  bool m_needsRecreate{false};
  bool m_readable{false};
};

} // namespace sonnet::rhi
