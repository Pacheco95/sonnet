#pragma once

#include <sonnet/rhi/Types.h>

#include <cstdint>
#include <optional>

namespace sonnet::rhi {

struct SwapchainImage {
  ImageHandle image; // in ImageLayout::Undefined when acquired; transition to Present before endFrame
  glm::uvec2 extent;
  std::uint32_t index;
};

class ISwapchain {
public:
  virtual ~ISwapchain() = default;

  // Acquires the next image for this frame. Recreates the swapchain when it is out of date or a
  // resize was requested, and returns nullopt when there is nothing to render to (minimised
  // window). At most one acquire per frame; endFrame presents every image acquired.
  [[nodiscard]] virtual std::optional<SwapchainImage> acquire() = 0;

  // Marks the swapchain for recreation at the next acquire, e.g. from a resize event.
  virtual void requestResize() = 0;

  [[nodiscard]] virtual Format format() const = 0;
  [[nodiscard]] virtual glm::uvec2 extent() const = 0;
  [[nodiscard]] virtual std::uint32_t imageCount() const = 0;
  // Whether the images also have ImageUsage::TransferSrc, so a frame can be copied out of one
  // before it is presented: the editor's window screenshot (docs/editor.md, "Screenshots"). The
  // surface decides; every desktop driver, Lavapipe's headless surface and MoltenVK allow it.
  [[nodiscard]] virtual bool readable() const = 0;
};

} // namespace sonnet::rhi
