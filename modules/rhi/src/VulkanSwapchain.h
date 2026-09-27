#pragma once

#include <sonnet/rhi/Swapchain.h>

#include <sonnet/platform/Window.h>

#include <vulkan/vulkan_raii.hpp>

#include <cstdint>
#include <string_view>
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
  void suspend() override;
  core::Result<void> resume() override;
  bool suspended() const override {
    return m_suspended;
  }
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
  // Called by the device after a present that reported out of date.
  void markOutOfDate() noexcept {
    m_needsRecreate = true;
  }
  // Called by acquire and by the device after VK_SUBOPTIMAL_KHR. Recreates at the next acquire
  // unless the only thing that differs is a surface transform the swapchain chose not to follow.
  void markSuboptimal();
  // Called by acquire and by the device when the surface is gone, which on Android can happen
  // before the application hears it is going to the background: nothing is drawn until resume.
  void markSurfaceLost(std::string_view where);

private:
  // Creates the surface from the window and checks the graphics queue can present to it.
  void createSurface();
  // Returns false when the window has no drawable area.
  bool create(vk::SwapchainKHR oldSwapchain);
  void releaseImages();
  // The images, the swapchain, then the surface; the device must be idle.
  void releaseSurface();

  VulkanDevice &m_device;
  platform::IWindow &m_window;
  // Surface outlives the swapchain: declared first, destroyed last.
  vk::raii::SurfaceKHR m_surface{nullptr};
  vk::raii::SwapchainKHR m_swapchain{nullptr};
  std::vector<ImageHandle> m_images;
  std::vector<vk::raii::Semaphore> m_renderFinished;
  Format m_format{Format::Undefined};
  glm::uvec2 m_extent{0, 0};
  // Identity where the surface supports it: the compositor rotates (docs/rendering.md, "Rotation").
  vk::SurfaceTransformFlagBitsKHR m_preTransform{vk::SurfaceTransformFlagBitsKHR::eIdentity};
  bool m_needsRecreate{false};
  bool m_readable{false};
  bool m_suspended{false};
  bool m_surfaceLost{false};
};

} // namespace sonnet::rhi
