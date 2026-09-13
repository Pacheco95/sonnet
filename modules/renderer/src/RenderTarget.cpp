#include <sonnet/renderer/RenderTarget.h>

#include <sonnet/renderer/Renderer.h>

#include <sonnet/core/Log.h>

#include <format>
#include <utility>

namespace sonnet::renderer {

RenderTarget::RenderTarget(rhi::IDevice &device, std::string debugName)
    : m_device(device), m_debugName(std::move(debugName)) {
}

RenderTarget::~RenderTarget() {
  release();
}

void RenderTarget::resize(glm::uvec2 size) {
  if (size == m_size) {
    return;
  }
  release();
  m_size = size;
  if (size.x == 0 || size.y == 0) {
    return;
  }
  m_color = m_device.createImage(
      {.size = size,
       .format = Renderer::ColorFormat,
       .usage = rhi::ImageUsage::ColorAttachment | rhi::ImageUsage::Sampled | rhi::ImageUsage::TransferSrc,
       .debugName = std::format("{} color", m_debugName)});
  m_depth = m_device.createImage({.size = size,
                                  .format = Renderer::DepthFormat,
                                  .usage = rhi::ImageUsage::DepthAttachment,
                                  .debugName = std::format("{} depth", m_debugName)});
  SONNET_LOG_DEBUG("render target \"{}\" {}x{}", m_debugName, size.x, size.y);
}

void RenderTarget::release() {
  if (m_color) {
    m_device.destroyImage(m_color);
    m_color = {};
  }
  if (m_depth) {
    m_device.destroyImage(m_depth);
    m_depth = {};
  }
  m_size = {0, 0};
}

} // namespace sonnet::renderer
