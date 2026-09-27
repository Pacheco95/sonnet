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

VulkanSwapchain::VulkanSwapchain(VulkanDevice &device, platform::IWindow &window) : m_device(device), m_window(window) {
  createSurface();
  if (!create(VK_NULL_HANDLE)) {
    m_needsRecreate = true;
  }
}

VulkanSwapchain::~VulkanSwapchain() {
  m_device.waitIdle();
  releaseImages();
}

void VulkanSwapchain::createSurface() {
  m_surface = vk::raii::SurfaceKHR{m_device.instance(), m_window.createVulkanSurface(*m_device.instance())};
  if (!m_device.physicalDevice().getSurfaceSupportKHR(m_device.graphicsFamily(), *m_surface)) {
    m_surface.clear();
    throw core::Exception{"the graphics queue cannot present to this window", core::ErrorCategory::Graphics};
  }
  // Colour attachment is the only usage a surface has to allow; copying out is asked for where it
  // is offered, which does not change with the swapchain's size.
  m_readable = static_cast<bool>(m_device.physicalDevice().getSurfaceCapabilitiesKHR(*m_surface).supportedUsageFlags &
                                 vk::ImageUsageFlagBits::eTransferSrc);
}

bool VulkanSwapchain::create(vk::SwapchainKHR oldSwapchain) {
  const glm::uvec2 size = m_window.pixelSize();
  if (size.x == 0 || size.y == 0) {
    return false;
  }

  // Android reports the display's rotation as the surface's current transform, and a swapchain
  // that takes it promises frames already drawn rotated. The engine draws them upright at the
  // window's size, so it asks for identity and the compositor turns the frame (issue #47).
  const vk::SurfaceCapabilitiesKHR capabilities = m_device.physicalDevice().getSurfaceCapabilitiesKHR(*m_surface);
  const vk::SurfaceTransformFlagBitsKHR preTransform =
      (capabilities.supportedTransforms & vk::SurfaceTransformFlagBitsKHR::eIdentity)
          ? vk::SurfaceTransformFlagBitsKHR::eIdentity
          : capabilities.currentTransform;

  VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  if (m_readable) {
    usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  }
  // Mailbox never waits for the display, so the GPU draws frames nobody sees. On a phone that
  // kept the Adreno 830 99 % busy and throttled within two minutes, so Android waits for vsync.
#if defined(__ANDROID__)
  constexpr VkPresentModeKHR desiredPresentMode = VK_PRESENT_MODE_FIFO_KHR;
#else
  constexpr VkPresentModeKHR desiredPresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
#endif
  vkb::SwapchainBuilder builder{static_cast<VkPhysicalDevice>(*m_device.physicalDevice()),
                                static_cast<VkDevice>(*m_device.device()), static_cast<VkSurfaceKHR>(*m_surface),
                                m_device.graphicsFamily(), m_device.graphicsFamily()};
  // UNORM: what reaches the swapchain is already display-encoded, by the scene's output pass and
  // by Dear ImGui's vertex colours, so an sRGB view would encode it twice (docs/rendering.md).
  builder.set_desired_format({VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
      .add_fallback_format({VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
      .add_fallback_format({VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
      .add_fallback_format({VK_FORMAT_R8G8B8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
      .set_desired_present_mode(desiredPresentMode)
      .add_fallback_present_mode(VK_PRESENT_MODE_FIFO_KHR)
      .set_desired_extent(size.x, size.y)
      .set_desired_min_image_count(3)
      .set_image_usage_flags(usage)
      .set_pre_transform_flags(static_cast<VkSurfaceTransformFlagBitsKHR>(preTransform))
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
  m_preTransform = preTransform;

  vkb::Result<std::vector<VkImage>> images = swapchain.get_images();
  if (!images) {
    throw core::Exception{std::format("querying swapchain images: {}", images.error().message()),
                          core::ErrorCategory::Graphics};
  }
  for (std::size_t i = 0; i < images.value().size(); ++i) {
    m_images.push_back(m_device.registerExternalImage(
        vk::Image{images.value()[i]}, ImageDesc{.size = m_extent,
                                                .format = m_format,
                                                .usage = ImageUsage::ColorAttachment | ImageUsage::TransferDst |
                                                         (m_readable ? ImageUsage::TransferSrc : ImageUsage::None),
                                                .debugName = std::format("swapchain image {}", i)}));
    m_renderFinished.emplace_back(m_device.device(), vk::SemaphoreCreateInfo{});
    m_device.setDebugName(vk::ObjectType::eSemaphore,
                          reinterpret_cast<std::uint64_t>(static_cast<VkSemaphore>(*m_renderFinished.back())),
                          std::format("swapchain image {} render finished", i));
  }
  m_needsRecreate = false;
  SONNET_LOG_DEBUG("swapchain {}x{}, {} images, {}, {}, surface transform {}", m_extent.x, m_extent.y, m_images.size(),
                   vk::to_string(static_cast<vk::Format>(swapchain.image_format)),
                   vk::to_string(static_cast<vk::PresentModeKHR>(swapchain.present_mode)),
                   vk::to_string(capabilities.currentTransform));
  return true;
}

void VulkanSwapchain::releaseImages() {
  for (const ImageHandle handle : m_images) {
    m_device.destroyImage(handle);
  }
  m_images.clear();
  m_renderFinished.clear();
}

void VulkanSwapchain::releaseSurface() {
  releaseImages();
  m_swapchain.clear();
  m_surface.clear();
}

void VulkanSwapchain::requestResize() {
  m_needsRecreate = true;
}

void VulkanSwapchain::suspend() {
  if (m_suspended) {
    SONNET_LOG_DEBUG("swapchain already suspended");
    return;
  }
  // Nothing in flight may still use the images, and the swapchain goes before its surface.
  m_device.waitIdle();
  releaseSurface();
  m_suspended = true;
  m_surfaceLost = false;
  m_needsRecreate = false;
  SONNET_LOG_INFO("swapchain suspended");
}

core::Result<void> VulkanSwapchain::resume() {
  if (!m_suspended && !m_surfaceLost) {
    SONNET_LOG_DEBUG("swapchain resumed without a suspend: nothing to do");
    return {};
  }
  if (!m_suspended) {
    suspend(); // the surface was lost: release what is left of it first
  }
  try {
    createSurface();
    if (!create(VK_NULL_HANDLE)) {
      m_needsRecreate = true; // no drawable area yet: acquire creates it once there is one
    }
  } catch (const core::Exception &e) {
    releaseSurface();
    return std::unexpected{e.error()};
  } catch (const vk::SystemError &e) {
    releaseSurface();
    return std::unexpected{
        core::Error{std::format("resuming the swapchain: {}", e.what()), core::ErrorCategory::Graphics}};
  }
  m_suspended = false;
  if (m_needsRecreate) {
    SONNET_LOG_INFO("swapchain resumed; the window has no drawable area yet");
  } else {
    SONNET_LOG_INFO("swapchain resumed at {}x{}", m_extent.x, m_extent.y);
  }
  return {};
}

void VulkanSwapchain::markSuboptimal() {
  if (m_needsRecreate || m_surfaceLost || m_suspended) {
    return;
  }
  // With an identity pre-transform, Android reports suboptimal on every present while the display
  // is turned, and recreating would not stop it. The swapchain stays unless the surface's size
  // changed too, which the window's resize event also reports, maybe a frame later.
  vk::SurfaceCapabilitiesKHR capabilities;
  try {
    capabilities = m_device.physicalDevice().getSurfaceCapabilitiesKHR(*m_surface);
  } catch (const vk::SystemError &) {
    m_needsRecreate = true; // let the recreate report what went wrong
    return;
  }
  const bool transformOnly = capabilities.currentTransform != m_preTransform &&
                             capabilities.currentExtent.width == m_extent.x &&
                             capabilities.currentExtent.height == m_extent.y;
  if (!transformOnly) {
    m_needsRecreate = true;
  }
}

void VulkanSwapchain::markSurfaceLost(std::string_view where) {
  if (m_surfaceLost) {
    return;
  }
  m_surfaceLost = true;
  SONNET_LOG_WARN("{}: the surface is lost; nothing is drawn until the swapchain is resumed", where);
}

std::optional<SwapchainImage> VulkanSwapchain::acquire() {
  SONNET_ZONE();
  if (m_suspended || m_surfaceLost) {
    return std::nullopt;
  }
  for (int attempt = 0; attempt < 2; ++attempt) {
    if (m_needsRecreate) {
      m_device.waitIdle();
      try {
        if (!create(*m_swapchain)) {
          return std::nullopt; // minimised: nothing to draw until the next resize
        }
      } catch (const core::Exception &e) {
        // The frame path does not throw. A surface the OS has taken away fails here too.
        SONNET_LOG_ERROR("{}", e.what());
        markSurfaceLost("recreating the swapchain");
        return std::nullopt;
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
    if (result == VK_ERROR_SURFACE_LOST_KHR) {
      markSurfaceLost("vkAcquireNextImageKHR");
      return std::nullopt;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
      SONNET_LOG_ERROR("vkAcquireNextImageKHR failed: {}", vk::to_string(static_cast<vk::Result>(result)));
      return std::nullopt;
    }
    if (result == VK_SUBOPTIMAL_KHR) {
      markSuboptimal(); // the image was acquired and must be presented; any recreate is next frame
    }
    m_device.addPendingPresent(*this, index, imageAvailable);
    return SwapchainImage{m_images[index], m_extent, index};
  }
  return std::nullopt;
}

} // namespace sonnet::rhi
