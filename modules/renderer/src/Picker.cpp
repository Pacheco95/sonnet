#include <sonnet/renderer/Picker.h>

#include <sonnet/core/Log.h>

#include <cstring>
#include <format>

namespace sonnet::renderer {

Picker::Picker(rhi::IDevice &device) : m_device(device) {
}

Picker::~Picker() {
  for (Slot &slot : m_slots) {
    if (slot.buffer) {
      m_device.destroyBuffer(slot.buffer);
    }
  }
}

void Picker::request(glm::uvec2 pixel) {
  m_request = pixel;
}

std::optional<std::uint32_t> Picker::poll() {
  Slot &slot = m_slots[m_frame % rhi::FramesInFlight];
  ++m_frame;
  if (!slot.pending) {
    return std::nullopt;
  }
  slot.pending = false;
  if (slot.pixel.x >= slot.imageSize.x || slot.pixel.y >= slot.imageSize.y) {
    return std::nullopt; // the viewport shrank between the request and the copy
  }
  const std::span<const std::byte> pixels = m_device.mappedRange(slot.buffer);
  const std::size_t offset = (std::size_t{slot.pixel.y} * slot.imageSize.x + slot.pixel.x) * sizeof(std::uint32_t);
  std::uint32_t id = 0;
  std::memcpy(&id, pixels.data() + offset, sizeof(id));
  return id;
}

void Picker::addPass(RenderGraph &graph, GraphImage ids, glm::uvec2 size) {
  if (!m_request || size.x == 0 || size.y == 0) {
    m_request.reset();
    return;
  }
  // poll advanced the counter; the slot recorded now is the one beginFrame just waited for.
  Slot &slot = m_slots[(m_frame + rhi::FramesInFlight - 1) % rhi::FramesInFlight];
  const std::uint64_t needed = std::uint64_t{size.x} * size.y * sizeof(std::uint32_t);
  if (slot.capacity < needed) {
    if (slot.buffer) {
      m_device.destroyBuffer(slot.buffer);
    }
    slot.buffer = m_device.createBuffer({.size = needed,
                                         .usage = rhi::BufferUsage::TransferDst,
                                         .memory = rhi::MemoryUsage::GpuToCpu,
                                         .debugName = std::format("pick readback {}", &slot - m_slots.data())});
    slot.capacity = needed;
  }
  slot.imageSize = size;
  slot.pixel = *m_request;
  slot.pending = true;
  m_request.reset();
  const rhi::BufferHandle buffer = slot.buffer;
  graph.addPass(
      "pick readback", [&](PassBuilder &builder) { builder.transferSrc(ids); },
      [ids, buffer](rhi::ICommandList &commands, const PassResources &resources) {
        commands.copyImageToBuffer(resources.image(ids), buffer);
      });
}

} // namespace sonnet::renderer
