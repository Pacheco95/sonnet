#include <sonnet/rhi/NullDevice.h>

#include "OwnerThread.h"

#include <sonnet/core/Assert.h>
#include <sonnet/core/Log.h>

#include <algorithm>
#include <format>
#include <optional>
#include <string_view>

namespace sonnet::rhi {

namespace {

constexpr std::uint64_t TransientBufferSize = 8u << 20;
constexpr std::uint64_t TransientAlignment = 256;

std::string_view toString(ImageLayout layout) noexcept {
  switch (layout) {
  case ImageLayout::Undefined:
    return "Undefined";
  case ImageLayout::General:
    return "General";
  case ImageLayout::ColorAttachment:
    return "ColorAttachment";
  case ImageLayout::DepthAttachment:
    return "DepthAttachment";
  case ImageLayout::ShaderReadOnly:
    return "ShaderReadOnly";
  case ImageLayout::TransferSrc:
    return "TransferSrc";
  case ImageLayout::TransferDst:
    return "TransferDst";
  case ImageLayout::Present:
    return "Present";
  }
  return "?";
}

std::string_view toString(LoadOp op) noexcept {
  switch (op) {
  case LoadOp::Load:
    return "load";
  case LoadOp::Clear:
    return "clear";
  case LoadOp::DontCare:
    return "dontcare";
  }
  return "?";
}

} // namespace

class NullCommandList final : public ICommandList {
public:
  explicit NullCommandList(NullDevice &device) : m_device(device) {
  }

  void barrier(std::span<const ImageBarrier> barriers) override {
    for (const ImageBarrier &barrier : barriers) {
      m_device.m_trace.push_back(std::format("barrier {} {}->{}", m_device.imageName(barrier.image),
                                             toString(barrier.oldLayout), toString(barrier.newLayout)));
    }
  }
  void memoryBarrier(const MemoryBarrier &) override {
    m_device.m_trace.emplace_back("memoryBarrier");
  }
  void beginRendering(const RenderingDesc &desc) override {
    SONNET_ASSERT(!m_rendering, "beginRendering while rendering");
    m_rendering = true;
    std::string line = "beginRendering";
    for (const ColorAttachment &color : desc.colors) {
      line += std::format(" color {} {}", m_device.imageName(color.image), toString(color.load));
    }
    if (desc.depth != nullptr) {
      line += std::format(" depth {} {}", m_device.imageName(desc.depth->image), toString(desc.depth->load));
    }
    m_device.m_trace.push_back(std::move(line));
  }
  void endRendering() override {
    SONNET_ASSERT(m_rendering, "endRendering without beginRendering");
    m_rendering = false;
    m_device.m_trace.emplace_back("endRendering");
  }
  void bindPipeline(PipelineHandle pipeline) override {
    const NullDevice::Pipeline *resource = m_device.m_pipelines.find(pipeline);
    SONNET_ASSERT(resource != nullptr, "binding a stale pipeline handle");
    m_compute = resource->compute;
    m_device.m_trace.push_back(std::format("bindPipeline \"{}\"", resource->debugName));
  }
  void bindBuffers(std::span<const BufferBinding> bindings) override {
    for (const BufferBinding &binding : bindings) {
      m_device.m_trace.push_back(std::format("bindBuffer {} {} offset {}", binding.binding,
                                             m_device.bufferName(binding.buffer), binding.offset));
    }
  }
  void bindImages(std::span<const ImageBinding> bindings) override {
    for (const ImageBinding &binding : bindings) {
      m_device.m_trace.push_back(std::format("bindImage {} {}", binding.binding, m_device.imageName(binding.image)));
    }
  }
  void pushConstants(std::span<const std::byte> data) override {
    SONNET_ASSERT(data.size() <= PushConstantSize && data.size() % 4 == 0, "push constants: {} bytes", data.size());
    m_device.m_trace.push_back(std::format("pushConstants {} bytes", data.size()));
  }
  void setFrontFace(FrontFace frontFace) override {
    m_device.m_trace.push_back(
        std::format("setFrontFace {}", frontFace == FrontFace::Clockwise ? "Clockwise" : "CounterClockwise"));
  }
  void bindIndexBuffer(BufferHandle buffer, IndexType) override {
    m_device.m_trace.push_back(std::format("bindIndexBuffer {}", m_device.bufferName(buffer)));
  }
  void draw(std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t, std::uint32_t) override {
    SONNET_ASSERT(m_rendering, "draw outside beginRendering/endRendering");
    m_device.m_trace.push_back(std::format("draw {} x{}", vertexCount, instanceCount));
  }
  void drawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t, std::int32_t,
                   std::uint32_t) override {
    SONNET_ASSERT(m_rendering, "drawIndexed outside beginRendering/endRendering");
    m_device.m_trace.push_back(std::format("drawIndexed {} x{}", indexCount, instanceCount));
  }
  void drawIndexedIndirect(BufferHandle commands, std::uint64_t, std::uint32_t drawCount) override {
    SONNET_ASSERT(m_rendering, "drawIndexedIndirect outside beginRendering/endRendering");
    m_device.m_trace.push_back(
        std::format("drawIndexedIndirect {} count {}", m_device.bufferName(commands), drawCount));
  }
  void dispatch(std::uint32_t groupsX, std::uint32_t groupsY, std::uint32_t groupsZ) override {
    SONNET_ASSERT(m_compute && !m_rendering, "dispatch needs a compute pipeline outside rendering");
    m_device.m_trace.push_back(std::format("dispatch {} {} {}", groupsX, groupsY, groupsZ));
  }
  void copyImageToBuffer(ImageHandle image, BufferHandle buffer) override {
    m_device.m_trace.push_back(
        std::format("copyImageToBuffer {} -> {}", m_device.imageName(image), m_device.bufferName(buffer)));
  }
  void writeTimestamp(std::uint32_t index) override {
    SONNET_ASSERT(index < MaxTimestamps, "timestamp index {} out of range", index);
    NullDevice::Frame &frame = m_device.m_frames[m_device.m_frameIndex];
    frame.timestampCount = std::max(frame.timestampCount, index + 1);
    m_device.m_trace.push_back(std::format("timestamp {}", index));
  }

  bool rendering() const noexcept {
    return m_rendering;
  }

private:
  NullDevice &m_device;
  bool m_rendering{false};
  bool m_compute{false};
};

namespace {

class NullSwapchain final : public ISwapchain {
public:
  NullSwapchain(NullDevice &device, platform::IWindow &window) : m_device(device), m_window(window) {
    create();
  }
  ~NullSwapchain() override {
    for (const ImageHandle image : m_images) {
      m_device.destroyImage(image);
    }
  }
  NullSwapchain(const NullSwapchain &) = delete;
  NullSwapchain &operator=(const NullSwapchain &) = delete;

  std::optional<SwapchainImage> acquire() override {
    if (m_needsRecreate) {
      for (const ImageHandle image : m_images) {
        m_device.destroyImage(image);
      }
      m_images.clear();
      create();
    }
    if (m_extent.x == 0 || m_extent.y == 0) {
      return std::nullopt;
    }
    const std::uint32_t index = m_next;
    m_next = (m_next + 1) % static_cast<std::uint32_t>(m_images.size());
    return SwapchainImage{m_images[index], m_extent, index};
  }
  void requestResize() override {
    m_needsRecreate = true;
  }
  Format format() const override {
    return Format::B8G8R8A8Unorm;
  }
  glm::uvec2 extent() const override {
    return m_extent;
  }
  std::uint32_t imageCount() const override {
    return static_cast<std::uint32_t>(m_images.size());
  }

private:
  void create() {
    m_extent = m_window.pixelSize();
    m_needsRecreate = false;
    if (m_extent.x == 0 || m_extent.y == 0) {
      return;
    }
    for (std::uint32_t i = 0; i < 3; ++i) {
      m_images.push_back(m_device.createImage({.size = m_extent,
                                               .format = format(),
                                               .usage = ImageUsage::ColorAttachment | ImageUsage::TransferDst,
                                               .debugName = std::format("swapchain image {}", i)}));
    }
  }

  NullDevice &m_device;
  platform::IWindow &m_window;
  std::vector<ImageHandle> m_images;
  glm::uvec2 m_extent{0, 0};
  std::uint32_t m_next{0};
  bool m_needsRecreate{false};
};

} // namespace

NullDevice::NullDevice() : m_commandList(std::make_unique<NullCommandList>(*this)) {
  m_info.deviceName = "null";
  m_info.driverName = "null";
  m_info.apiVersion = 0;
  m_info.timestampsSupported = true;
  m_info.blockCompressionSupported = true;
  for (std::uint32_t i = 0; i < FramesInFlight; ++i) {
    m_frames[i].transientBuffer = createBuffer({.size = TransientBufferSize,
                                                .usage = BufferUsage::Uniform | BufferUsage::Storage,
                                                .memory = MemoryUsage::CpuToGpu,
                                                .debugName = std::format("frame {} transient", i)});
  }
}

NullDevice::~NullDevice() {
  for (Frame &frame : m_frames) {
    m_buffers.remove(frame.transientBuffer);
  }
  m_buffers.forEach([](BufferHandle handle, Buffer &buffer) {
    SONNET_LOG_WARN("leaked buffer \"{}\" ({}:{})", buffer.desc.debugName, handle.index, handle.generation);
  });
  m_images.forEach([](ImageHandle handle, Image &image) {
    SONNET_LOG_WARN("leaked image \"{}\" ({}:{})", image.desc.debugName, handle.index, handle.generation);
  });
}

std::unique_ptr<ISwapchain> NullDevice::createSwapchain(platform::IWindow &window) {
  assertOwnerThread("createSwapchain");
  return std::make_unique<NullSwapchain>(*this, window);
}

BufferHandle NullDevice::createBuffer(const BufferDesc &desc) {
  assertOwnerThread("createBuffer");
  SONNET_ASSERT(desc.size > 0, "buffer \"{}\" has no size", desc.debugName);
  Buffer buffer{desc, {}, 0};
  if (desc.memory != MemoryUsage::GpuOnly) {
    buffer.memory.resize(desc.size);
  }
  if (has(desc.usage, BufferUsage::Storage)) {
    buffer.address = m_nextAddress;
    m_nextAddress += (desc.size + 255) / 256 * 256;
  }
  return m_buffers.emplace(std::move(buffer));
}

void NullDevice::destroyBuffer(BufferHandle handle) {
  assertOwnerThread("destroyBuffer");
  if (!m_buffers.remove(handle)) {
    SONNET_LOG_WARN("destroyBuffer: stale handle {}:{}", handle.index, handle.generation);
  }
}

std::span<std::byte> NullDevice::mappedRange(BufferHandle handle) {
  Buffer *buffer = m_buffers.find(handle);
  if (buffer == nullptr) {
    return {};
  }
  return buffer->memory;
}

std::uint64_t NullDevice::bufferAddress(BufferHandle handle) const {
  const Buffer *buffer = m_buffers.find(handle);
  return buffer != nullptr ? buffer->address : 0;
}

ImageHandle NullDevice::createImage(const ImageDesc &desc) {
  assertOwnerThread("createImage");
  SONNET_ASSERT(desc.size.x > 0 && desc.size.y > 0, "image \"{}\" has no size", desc.debugName);
  SONNET_ASSERT(desc.mipLevels >= 1 && desc.mipLevels <= fullMipCount(desc.size), "image \"{}\": {} mip levels",
                desc.debugName, desc.mipLevels);
  Image image{desc, InvalidBindlessIndex, {}};
  if (has(desc.usage, ImageUsage::Sampled)) {
    image.sampledIndex = desc.cube                    ? m_nextCubeIndex++
                         : isDepthFormat(desc.format) ? m_nextDepthIndex++
                                                      : m_nextSampledIndex++;
  }
  return m_images.emplace(std::move(image));
}

std::uint32_t NullDevice::sampledImageIndex(ImageHandle handle) const {
  const Image *image = m_images.find(handle);
  return image != nullptr ? image->sampledIndex : InvalidBindlessIndex;
}

std::uint32_t NullDevice::storageImageIndex(ImageHandle handle, std::uint32_t mipLevel) {
  assertOwnerThread("storageImageIndex");
  Image *image = m_images.find(handle);
  if (image == nullptr || !has(image->desc.usage, ImageUsage::Storage) || mipLevel >= image->desc.mipLevels) {
    return InvalidBindlessIndex;
  }
  if (image->storageIndices.empty()) {
    image->storageIndices.assign(image->desc.mipLevels, InvalidBindlessIndex);
  }
  if (image->storageIndices[mipLevel] == InvalidBindlessIndex) {
    image->storageIndices[mipLevel] = m_nextStorageIndex++;
  }
  return image->storageIndices[mipLevel];
}

std::uint32_t NullDevice::storageBufferIndex(BufferHandle handle) {
  assertOwnerThread("storageBufferIndex");
  Buffer *buffer = m_buffers.find(handle);
  if (buffer == nullptr || !has(buffer->desc.usage, BufferUsage::Storage)) {
    return InvalidBindlessIndex;
  }
  if (buffer->storageBufferIndex == InvalidBindlessIndex) {
    buffer->storageBufferIndex = m_nextStorageBufferIndex++;
  }
  return buffer->storageBufferIndex;
}

SamplerHandle NullDevice::createSampler(const SamplerDesc &desc) {
  assertOwnerThread("createSampler");
  const std::uint32_t index = desc.compare ? m_nextComparisonSamplerIndex++ : m_nextSamplerIndex++;
  return m_samplers.emplace(Sampler{desc, index});
}

void NullDevice::destroySampler(SamplerHandle handle) {
  assertOwnerThread("destroySampler");
  if (!m_samplers.remove(handle)) {
    SONNET_LOG_WARN("destroySampler: stale handle {}:{}", handle.index, handle.generation);
  }
}

std::uint32_t NullDevice::samplerIndex(SamplerHandle handle) const {
  const Sampler *sampler = m_samplers.find(handle);
  return sampler != nullptr ? sampler->index : InvalidBindlessIndex;
}

void NullDevice::uploadBuffer(BufferHandle handle, std::uint64_t offset, std::span<const std::byte> data) {
  assertOwnerThread("uploadBuffer");
  Buffer *buffer = m_buffers.find(handle);
  if (buffer == nullptr) {
    SONNET_LOG_WARN("uploadBuffer: stale handle {}:{}", handle.index, handle.generation);
    return;
  }
  SONNET_ASSERT(has(buffer->desc.usage, BufferUsage::TransferDst), "buffer \"{}\" is not a transfer destination",
                buffer->desc.debugName);
  SONNET_ASSERT(offset + data.size() <= buffer->desc.size, "upload of {} bytes at {} overflows buffer \"{}\"",
                data.size(), offset, buffer->desc.debugName);
  if (!buffer->memory.empty()) {
    std::ranges::copy(data, buffer->memory.begin() + static_cast<std::ptrdiff_t>(offset));
  }
  m_trace.push_back(std::format("uploadBuffer \"{}\" {} bytes at {}", buffer->desc.debugName, data.size(), offset));
}

void NullDevice::uploadImage(ImageHandle handle, std::span<const ImageUpload> uploads) {
  assertOwnerThread("uploadImage");
  const Image *image = m_images.find(handle);
  if (image == nullptr) {
    SONNET_LOG_WARN("uploadImage: stale handle {}:{}", handle.index, handle.generation);
    return;
  }
  SONNET_ASSERT(has(image->desc.usage, ImageUsage::TransferDst), "image \"{}\" is not a transfer destination",
                image->desc.debugName);
  for (const ImageUpload &upload : uploads) {
    SONNET_ASSERT(upload.mipLevel < image->desc.mipLevels && upload.layer < image->desc.layers(),
                  "upload to level {} layer {} of image \"{}\"", upload.mipLevel, upload.layer, image->desc.debugName);
    SONNET_ASSERT(upload.data.size() == levelByteSize(image->desc.format, mipSize(image->desc.size, upload.mipLevel)),
                  "level {} of image \"{}\" takes {} bytes, {} given", upload.mipLevel, image->desc.debugName,
                  levelByteSize(image->desc.format, mipSize(image->desc.size, upload.mipLevel)), upload.data.size());
    m_trace.push_back(std::format("uploadImage \"{}\" level {} layer {} {} bytes", image->desc.debugName,
                                  upload.mipLevel, upload.layer, upload.data.size()));
  }
}

void NullDevice::destroyImage(ImageHandle handle) {
  assertOwnerThread("destroyImage");
  if (!m_images.remove(handle)) {
    SONNET_LOG_WARN("destroyImage: stale handle {}:{}", handle.index, handle.generation);
  }
}

const ImageDesc &NullDevice::imageDesc(ImageHandle handle) const {
  return m_images.get(handle).desc;
}

ShaderHandle NullDevice::createShader(const ShaderDesc &desc) {
  assertOwnerThread("createShader");
  return m_shaders.emplace(Shader{desc.debugName});
}

void NullDevice::destroyShader(ShaderHandle handle) {
  assertOwnerThread("destroyShader");
  if (!m_shaders.remove(handle)) {
    SONNET_LOG_WARN("destroyShader: stale handle {}:{}", handle.index, handle.generation);
  }
}

PipelineHandle NullDevice::createGraphicsPipeline(const GraphicsPipelineDesc &desc) {
  assertOwnerThread("createGraphicsPipeline");
  SONNET_ASSERT(m_shaders.contains(desc.shader), "pipeline \"{}\": stale shader handle", desc.debugName);
  return m_pipelines.emplace(Pipeline{desc.debugName, false});
}

PipelineHandle NullDevice::createComputePipeline(const ComputePipelineDesc &desc) {
  assertOwnerThread("createComputePipeline");
  SONNET_ASSERT(m_shaders.contains(desc.shader), "compute pipeline \"{}\": stale shader handle", desc.debugName);
  return m_pipelines.emplace(Pipeline{desc.debugName, true});
}

void NullDevice::destroyPipeline(PipelineHandle handle) {
  assertOwnerThread("destroyPipeline");
  if (!m_pipelines.remove(handle)) {
    SONNET_LOG_WARN("destroyPipeline: stale handle {}:{}", handle.index, handle.generation);
  }
}

bool NullDevice::isValid(BufferHandle handle) const {
  return m_buffers.contains(handle);
}
bool NullDevice::isValid(ImageHandle handle) const {
  return m_images.contains(handle);
}
bool NullDevice::isValid(SamplerHandle handle) const {
  return m_samplers.contains(handle);
}
bool NullDevice::isValid(ShaderHandle handle) const {
  return m_shaders.contains(handle);
}
bool NullDevice::isValid(PipelineHandle handle) const {
  return m_pipelines.contains(handle);
}

ICommandList &NullDevice::beginFrame() {
  assertOwnerThread("beginFrame");
  SONNET_ASSERT(!m_recording, "beginFrame called twice without endFrame");
  Frame &frame = m_frames[m_frameIndex];
  // The previous use of this slot "completed": its timestamps read back as zero.
  frame.timestampResults.assign(frame.timestampCount, 0);
  frame.timestampCount = 0;
  frame.transientOffset = 0;
  m_trace.clear();
  m_recording = true;
  return *m_commandList;
}

void NullDevice::endFrame() {
  assertOwnerThread("endFrame");
  SONNET_ASSERT(m_recording, "endFrame called without beginFrame");
  SONNET_ASSERT(!m_commandList->rendering(), "endFrame inside beginRendering/endRendering");
  m_recording = false;
  m_frameIndex = (m_frameIndex + 1) % FramesInFlight;
}

void NullDevice::waitIdle() {
  assertOwnerThread("waitIdle");
}

TransientAllocation NullDevice::allocateTransient(std::uint64_t size) {
  assertOwnerThread("allocateTransient");
  SONNET_ASSERT(m_recording, "allocateTransient outside beginFrame/endFrame");
  Frame &frame = m_frames[m_frameIndex];
  const std::uint64_t offset =
      (frame.transientOffset + TransientAlignment - 1) / TransientAlignment * TransientAlignment;
  if (size == 0 || offset + size > TransientBufferSize) {
    SONNET_LOG_ERROR("transient allocator exhausted: {} bytes requested at offset {}", size, offset);
    return {};
  }
  frame.transientOffset = offset + size;
  return TransientAllocation{frame.transientBuffer, offset, mappedRange(frame.transientBuffer).subspan(offset, size)};
}

std::span<const std::uint64_t> NullDevice::timestamps() const {
  return m_frames[m_frameIndex].timestampResults;
}

MemoryBudget NullDevice::memoryBudget() const {
  MemoryBudget budget;
  budget.heapCount = 1;
  std::uint64_t usage = 0;
  m_buffers.forEach([&](BufferHandle, const Buffer &buffer) { usage += buffer.desc.size; });
  m_images.forEach([&](ImageHandle, const Image &image) { usage += image.desc.byteSize(); });
  budget.heaps[0] = HeapBudget{usage, 1u << 30, true};
  return budget;
}

void NullDevice::assertOwnerThread(std::string_view what) const {
  detail::assertOwnerThread(m_ownerThread, what);
}

std::string NullDevice::imageName(ImageHandle handle) const {
  const Image *image = m_images.find(handle);
  return image != nullptr ? std::format("\"{}\"", image->desc.debugName) : "<stale image>";
}

std::string NullDevice::bufferName(BufferHandle handle) const {
  const Buffer *buffer = m_buffers.find(handle);
  return buffer != nullptr ? std::format("\"{}\"", buffer->desc.debugName) : "<stale buffer>";
}

std::unique_ptr<NullDevice> createNullDevice() {
  return std::make_unique<NullDevice>();
}

} // namespace sonnet::rhi
