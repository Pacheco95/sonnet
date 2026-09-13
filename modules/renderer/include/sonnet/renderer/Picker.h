#pragma once

#include <sonnet/renderer/RenderGraph.h>

#include <sonnet/core/Math.h>
#include <sonnet/rhi/Device.h>

#include <array>
#include <cstdint>
#include <optional>

namespace sonnet::renderer {

// Reads an entity id back from the id image (docs/rendering.md, "Editor viewport and ImGui").
// A request copies this frame's id image into the frame slot's readback buffer; the answer is
// available once the device has waited for that frame again, FramesInFlight frames later, so
// nothing stalls. One request per frame; a later one in the same frame replaces it.
class Picker {
public:
  explicit Picker(rhi::IDevice &device);
  ~Picker();
  Picker(const Picker &) = delete;
  Picker &operator=(const Picker &) = delete;

  // The pixel to read from the id image that this frame's graph draws.
  void request(glm::uvec2 pixel);
  [[nodiscard]] bool hasRequest() const noexcept {
    return m_request.has_value();
  }

  // Once per frame, after the device's beginFrame: the id answered by the request made
  // FramesInFlight frames ago, if there was one. Advances the frame slot.
  [[nodiscard]] std::optional<std::uint32_t> poll();

  // After poll, when a request is pending: declares the copy of `ids`, an image of `size`
  // pixels, into the slot's readback buffer, after the passes that write it.
  void addPass(RenderGraph &graph, GraphImage ids, glm::uvec2 size);

private:
  struct Slot {
    rhi::BufferHandle buffer;
    std::uint64_t capacity{0};
    glm::uvec2 imageSize{0, 0};
    glm::uvec2 pixel{0, 0};
    bool pending{false};
  };

  rhi::IDevice &m_device;
  std::array<Slot, rhi::FramesInFlight> m_slots;
  std::uint64_t m_frame{0};
  std::optional<glm::uvec2> m_request;
};

} // namespace sonnet::renderer
