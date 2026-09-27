#include <sonnet/runtime/Screenshot.h>

#include <sonnet/core/File.h>
#include <sonnet/core/Log.h>

#include <vector>

// stb_image_write is header-only; this is its one implementation in the engine. The macros are
// stb's names.
// NOLINTBEGIN(readability-identifier-naming)
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
// NOLINTEND(readability-identifier-naming)
#include <stb_image_write.h>

namespace sonnet::runtime {

core::Result<void> writeScreenshot(const std::filesystem::path &file, glm::uvec2 size, rhi::Format format,
                                   std::span<const std::byte> pixels) {
  const std::size_t count = std::size_t{size.x} * size.y;
  if (pixels.size() < count * 4) {
    return std::unexpected(core::Error{"the screenshot's readback is smaller than its image", core::ErrorCategory::Io});
  }
  const bool bgra = format == rhi::Format::B8G8R8A8Unorm || format == rhi::Format::B8G8R8A8Srgb;
  std::vector<unsigned char> rgba(count * 4);
  for (std::size_t i = 0; i < count; ++i) {
    const auto channel = [&](std::size_t c) { return std::to_integer<unsigned char>(pixels[i * 4 + c]); };
    rgba[i * 4 + 0] = channel(bgra ? 2 : 0);
    rgba[i * 4 + 1] = channel(1);
    rgba[i * 4 + 2] = channel(bgra ? 0 : 2);
    rgba[i * 4 + 3] = 255;
  }
  std::vector<std::byte> png;
  const auto append = [](void *context, void *data, int length) {
    auto *out = static_cast<std::vector<std::byte> *>(context);
    const auto *bytes = static_cast<const std::byte *>(data);
    out->insert(out->end(), bytes, bytes + length);
  };
  if (stbi_write_png_to_func(append, &png, static_cast<int>(size.x), static_cast<int>(size.y), 4, rgba.data(),
                             static_cast<int>(size.x) * 4) == 0) {
    return std::unexpected(core::Error{"encoding the screenshot as PNG failed", core::ErrorCategory::Io});
  }
  return core::writeFile(file, png);
}

Screenshots::~Screenshots() {
  // Copies whose frame never finished, when their owner goes first: the device defers the release.
  for (const Readback &readback : m_readbacks) {
    m_device.destroyBuffer(readback.buffer);
  }
}

void Screenshots::add(renderer::RenderGraph &graph, renderer::GraphImage image, glm::uvec2 size, rhi::Format format,
                      std::filesystem::path file) {
  const rhi::BufferHandle buffer = m_device.createBuffer({.size = std::uint64_t{size.x} * size.y * 4,
                                                          .usage = rhi::BufferUsage::TransferDst,
                                                          .memory = rhi::MemoryUsage::GpuToCpu,
                                                          .debugName = "screenshot"});
  m_readbacks.push_back({.file = std::move(file), .buffer = buffer, .size = size, .format = format});
  graph.addPass(
      "screenshot", [&](renderer::PassBuilder &builder) { builder.transferSrc(image); },
      [image, buffer](rhi::ICommandList &cmd, const renderer::PassResources &resources) {
        cmd.copyImageToBuffer(resources.image(image), buffer);
      });
}

core::Result<void> Screenshots::write() {
  // A screenshot is a one-off: waiting for the frame is simpler than tracking its fence.
  m_device.waitIdle();
  core::Result<void> result;
  for (const Readback &readback : m_readbacks) {
    if (result) {
      result = writeScreenshot(readback.file, readback.size, readback.format, m_device.mappedRange(readback.buffer));
      if (result) {
        SONNET_LOG_INFO("screenshot {}x{} written to {}", readback.size.x, readback.size.y, readback.file.string());
      }
    }
    m_device.destroyBuffer(readback.buffer);
  }
  m_readbacks.clear();
  return result;
}

} // namespace sonnet::runtime
