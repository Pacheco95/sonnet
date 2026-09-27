#pragma once

#include <sonnet/core/Error.h>
#include <sonnet/core/Math.h>
#include <sonnet/renderer/RenderGraph.h>
#include <sonnet/rhi/Device.h>
#include <sonnet/rhi/Types.h>

#include <cstddef>
#include <filesystem>
#include <span>
#include <vector>

namespace sonnet::runtime {

// Tightly packed 8-bit RGBA or BGRA pixels, as copyImageToBuffer leaves them, written as an
// opaque PNG: the scene's alpha is not coverage and would punch holes in the image.
[[nodiscard]] core::Result<void> writeScreenshot(const std::filesystem::path &file, glm::uvec2 size, rhi::Format format,
                                                 std::span<const std::byte> pixels);

// The screenshots of one frame (docs/editor.md, "Screenshots"; docs/player.md, "Capture runs"):
// each is a pass copying an image of the frame's graph into a host-visible buffer, written as a
// PNG once the frame is done.
class Screenshots {
public:
  explicit Screenshots(rhi::IDevice &device) : m_device(device) {
  }
  ~Screenshots();
  Screenshots(const Screenshots &) = delete;
  Screenshots &operator=(const Screenshots &) = delete;

  // Declares a pass copying `image`, of `size` and an 8-bit four-channel `format`, into a new
  // readback buffer for `file`.
  void add(renderer::RenderGraph &graph, renderer::GraphImage image, glm::uvec2 size, rhi::Format format,
           std::filesystem::path file);
  [[nodiscard]] bool pending() const noexcept {
    return !m_readbacks.empty();
  }
  // After the frame that copied them was submitted: waits for the device, writes every PNG and
  // frees the buffers. The first failure is the result, and stops the writing.
  [[nodiscard]] core::Result<void> write();

private:
  // A screenshot's copy, waiting in a host-visible buffer for its frame to finish.
  struct Readback {
    std::filesystem::path file;
    rhi::BufferHandle buffer;
    glm::uvec2 size;
    rhi::Format format;
  };

  rhi::IDevice &m_device;
  std::vector<Readback> m_readbacks;
};

} // namespace sonnet::runtime
