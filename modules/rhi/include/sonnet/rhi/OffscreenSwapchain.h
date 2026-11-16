#pragma once

#include <sonnet/rhi/Swapchain.h>
#include <sonnet/rhi/Types.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace sonnet::platform {
class IWindow;
}

namespace sonnet::rhi {

class IDevice;

// A swapchain without a surface: three images the device owns, at the window's pixel size, handed
// out in turn and presented nowhere. It is the null device's swapchain, and the player's in a
// headless run (docs/player.md, "Headless runs"), where no display or compositor paces the
// presents: nothing waits but the device's frames in flight. An image comes back three acquires
// later, by when the frame that drew it has finished, since a frame slot waits for the frame
// FramesInFlight before it. It follows the Vulkan swapchain's rules otherwise: a resize request
// takes the window's new size at the next acquire, and the images go on suspend and come back at
// the window's size on resume. The device and the window outlive it.
class OffscreenSwapchain final : public ISwapchain {
public:
  static constexpr std::uint32_t ImageCount = 3;

  OffscreenSwapchain(IDevice &device, platform::IWindow &window);
  ~OffscreenSwapchain() override;
  OffscreenSwapchain(const OffscreenSwapchain &) = delete;
  OffscreenSwapchain &operator=(const OffscreenSwapchain &) = delete;

  [[nodiscard]] std::optional<SwapchainImage> acquire() override;
  void requestResize() override;
  void suspend() override;
  [[nodiscard]] core::Result<void> resume() override;
  [[nodiscard]] bool suspended() const override {
    return m_suspended;
  }
  [[nodiscard]] Format format() const override {
    return Format::B8G8R8A8Unorm;
  }
  [[nodiscard]] glm::uvec2 extent() const override {
    return m_extent;
  }
  [[nodiscard]] std::uint32_t imageCount() const override {
    return static_cast<std::uint32_t>(m_images.size());
  }
  [[nodiscard]] bool readable() const override {
    return true;
  }

private:
  void create();
  void release();

  IDevice &m_device;
  platform::IWindow &m_window;
  std::vector<ImageHandle> m_images;
  glm::uvec2 m_extent{0, 0};
  std::uint32_t m_next{0};
  bool m_needsRecreate{false};
  bool m_suspended{false};
};

} // namespace sonnet::rhi
