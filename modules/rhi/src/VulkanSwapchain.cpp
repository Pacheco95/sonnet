#include "VulkanSwapchain.h"

#include "VulkanDevice.h"
#include "VulkanTypes.h"

#include <sonnet/core/Error.h>
#include <sonnet/core/Log.h>
#include <sonnet/core/Profile.h>

#include <VkBootstrap.h>

#include <format>
#include <limits>

namespace sonnet::rhi {

VulkanSwapchain::VulkanSwapchain(VulkanDevice &device, platform::IWindow &window)
    : m_device(device), m_window(window), m_surface(device.instance(), window.createVulkanSurface(*device.instance())) {
  if (!device.physicalDevice().getSurfaceSupportKHR(device.graphicsFamily(), *m_surface)) {
    throw core::Exception{"the graphics queue cannot present to this window", core::ErrorCategory::Graphics};
  }
  if (!create(VK_NULL_HANDLE)) {
    m_needsRecreate = true;
  }
}

VulkanSwapchain::~VulkanSwapchain() {
  m_device.waitIdle();
  releaseImages();
}

bool VulkanSwapchain::create(vk::SwapchainKHR oldSwapchain) {
  const glm::uvec2 size = m_window.pixelSize();
  if (size.x == 0 || size.y == 0) {
    return false;
  }

  vkb::SwapchainBuilder builder{static_cast<VkPhysicalDevice>(*m_device.physicalDevice()),
                                static_cast<VkDevice>(*m_device.device()), static_cast<VkSurfaceKHR>(*m_surface),
                                m_device.graphicsFamily(), m_device.graphicsFamily()};
  // UNORM: what reaches the swapchain is already display-encoded, by the scene's output pass and
  // by Dear ImGui's vertex colours, so an sRGB view would encode it twice (docs/rendering.md).
  builder.set_desired_format({VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
      .add_fallback_format({VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
      .add_fallback_format({VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
      .add_fallback_format({VK_FORMAT_R8G8B8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
      .set_desired_present_mode(VK_PRESENT_MODE_MAILBOX_KHR)
      .add_fallback_present_mode(VK_PRESENT_MODE_FIFO_KHR)
      .set_desired_extent(size.x, size.y)
      .set_desired_min_image_count(3)
      .set_image_usage_flags(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
      .set_old_swapchain(static_cast<VkSwapchainKHR>(oldSwapchain));

  vkb::Result<vkb::Swapchain> built = builder.build();
  if (!built) {
    throw core::Exception{std::format("creating the swapchain: {}", built.error().message()),
                          core::ErrorCategory::Graphics};
  }
  vkb::Swapchain &swapchain = built.value();
  const Format format = fromVk(static_cast<vk::Format>(swapchain.image_format));
  if (format == Format::Undefined) {
    // Adopt so the handle is released, then fail.
    vk::raii::SwapchainKHR unusable{m_device.device(), swapchain.swapchain};
    throw core::Exception{std::format("swapchain format {} is not supported by the engine",
                                      vk::to_string(static_cast<vk::Format>(swapchain.image_format))),
                          core::ErrorCategory::Graphics};
  }

  // The old swapchain is retired by vkb through oldSwapchain; assignment destroys its wrapper.
  releaseImages();
  m_swapchain = vk::raii::SwapchainKHR{m_device.device(), swapchain.swapchain};
  m_format = format;
  m_extent = {swapchain.extent.width, swapchain.extent.height};

  vkb::Result<std::vector<VkImage>> images = swapchain.get_images();
  if (!images) {
    throw core::Exception{std::format("querying swapchain images: {}", images.error().message()),
                          core::ErrorCategory::Graphics};
  }
  for (std::size_t i = 0; i < images.value().size(); ++i) {
    m_images.push_back(m_device.registerExternalImage(
        vk::Image{images.value()[i]}, ImageDesc{.size = m_extent,
                                                .format = m_format,
                                                .usage = ImageUsage::ColorAttachment | ImageUsage::TransferDst,
                                                .debugName = std::format("swapchain image {}", i)}));
    m_renderFinished.emplace_back(m_device.device(), vk::SemaphoreCreateInfo{});
    m_device.setDebugName(vk::ObjectType::eSemaphore,
                          reinterpret_cast<std::uint64_t>(static_cast<VkSemaphore>(*m_renderFinished.back())),
                          std::format("swapchain image {} render finished", i));
  }
  m_needsRecreate = false;
  SONNET_LOG_DEBUG("swapchain {}x{}, {} images, {}, {}", m_extent.x, m_extent.y, m_images.size(),
                   vk::to_string(static_cast<vk::Format>(swapchain.image_format)),
                   vk::to_string(static_cast<vk::PresentModeKHR>(swapchain.present_mode)));
  return true;
}

void VulkanSwapchain::releaseImages() {
  for (const ImageHandle handle : m_images) {
    m_device.destroyImage(handle);
  }
  m_images.clear();
  m_renderFinished.clear();
}

void VulkanSwapchain::requestResize() {
  m_needsRecreate = true;
}

std::optional<SwapchainImage> VulkanSwapchain::acquire() {
  SONNET_ZONE();
  for (int attempt = 0; attempt < 2; ++attempt) {
    if (m_needsRecreate) {
      m_device.waitIdle();
      if (!create(*m_swapchain)) {
        return std::nullopt; // minimised: nothing to draw until the next resize
      }
    }
    const vk::Semaphore imageAvailable = m_device.currentImageAvailableSemaphore();
    std::uint32_t index = 0;
    const VkResult result = m_device.device().getDispatcher()->vkAcquireNextImageKHR(
        *m_device.device(), *m_swapchain, std::numeric_limits<std::uint64_t>::max(), imageAvailable, VK_NULL_HANDLE,
        &index);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
      m_needsRecreate = true;
      continue;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
      SONNET_LOG_ERROR("vkAcquireNextImageKHR failed: {}", vk::to_string(static_cast<vk::Result>(result)));
      return std::nullopt;
    }
    if (result == VK_SUBOPTIMAL_KHR) {
      m_needsRecreate = true; // the image was acquired and must be presented; recreate next frame
    }
    m_device.addPendingPresent(*this, index, imageAvailable);
    return SwapchainImage{m_images[index], m_extent, index};
  }
  return std::nullopt;
}

} // namespace sonnet::rhi
