#pragma once

#include <sonnet/core/Math.h>
#include <sonnet/rhi/Device.h>

#include <string>

namespace sonnet::renderer {

// A colour and depth pair in the renderer's formats, kept across frames so a viewport can
// display the colour image after the graph has drawn into it. The colour image can also be
// read back, for tests and screenshots.
class RenderTarget {
public:
  RenderTarget(rhi::IDevice &device, std::string debugName);
  ~RenderTarget();
  RenderTarget(const RenderTarget &) = delete;
  RenderTarget &operator=(const RenderTarget &) = delete;

  // Recreates the images when the size changes; a zero size releases them. The old images are
  // destroyed deferred by the device, so the frame in flight may still display them.
  void resize(glm::uvec2 size);

  [[nodiscard]] glm::uvec2 size() const noexcept {
    return m_size;
  }
  [[nodiscard]] bool isValid() const noexcept {
    return m_color.isValid();
  }
  [[nodiscard]] rhi::ImageHandle color() const noexcept {
    return m_color;
  }
  [[nodiscard]] rhi::ImageHandle depth() const noexcept {
    return m_depth;
  }

private:
  void release();

  rhi::IDevice &m_device;
  std::string m_debugName;
  glm::uvec2 m_size{0, 0};
  rhi::ImageHandle m_color;
  rhi::ImageHandle m_depth;
};

} // namespace sonnet::renderer
