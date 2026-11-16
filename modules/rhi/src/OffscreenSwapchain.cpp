#include <sonnet/rhi/OffscreenSwapchain.h>

#include <sonnet/platform/Window.h>
#include <sonnet/rhi/Device.h>

#include <format>

namespace sonnet::rhi {

OffscreenSwapchain::OffscreenSwapchain(IDevice &device, platform::IWindow &window)
    : m_device(device), m_window(window) {
  create();
}

OffscreenSwapchain::~OffscreenSwapchain() {
  release();
}

std::optional<SwapchainImage> OffscreenSwapchain::acquire() {
  if (m_suspended) {
    return std::nullopt;
  }
  if (m_needsRecreate) {
    release();
    create();
  }
  if (m_extent.x == 0 || m_extent.y == 0) {
    return std::nullopt;
  }
  const std::uint32_t index = m_next;
  m_next = (m_next + 1) % static_cast<std::uint32_t>(m_images.size());
  return SwapchainImage{m_images[index], m_extent, index};
}

void OffscreenSwapchain::requestResize() {
  if (m_window.pixelSize() != m_extent) {
    m_needsRecreate = true;
  }
}

void OffscreenSwapchain::suspend() {
  if (!m_suspended) {
    release();
    m_suspended = true;
  }
}

core::Result<void> OffscreenSwapchain::resume() {
  if (m_suspended) {
    m_suspended = false;
    create();
  }
  return {};
}

void OffscreenSwapchain::release() {
  // Destruction is deferred by the device until the frames that may still draw to them are done.
  for (const ImageHandle image : m_images) {
    m_device.destroyImage(image);
  }
  m_images.clear();
}

void OffscreenSwapchain::create() {
  m_extent = m_window.pixelSize();
  m_needsRecreate = false;
  m_next = 0;
  if (m_extent.x == 0 || m_extent.y == 0) {
    return;
  }
  for (std::uint32_t i = 0; i < ImageCount; ++i) {
    m_images.push_back(
        m_device.createImage({.size = m_extent,
                              .format = format(),
                              .usage = ImageUsage::ColorAttachment | ImageUsage::TransferDst | ImageUsage::TransferSrc,
                              .debugName = std::format("swapchain image {}", i)}));
  }
}

} // namespace sonnet::rhi
