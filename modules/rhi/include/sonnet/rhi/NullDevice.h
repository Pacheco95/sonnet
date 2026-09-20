#pragma once

#include <sonnet/rhi/Device.h>

#include <sonnet/core/HandlePool.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sonnet::rhi {

class NullCommandList;

// An IDevice without a GPU: handles, descriptions and host-visible memory behave as on the
// Vulkan device, recording produces a readable trace instead of work. Modules above rhi test
// against it (docs/rendering.md, "Testing").
class NullDevice final : public IDevice {
public:
  NullDevice();
  ~NullDevice() override;
  NullDevice(const NullDevice &) = delete;
  NullDevice &operator=(const NullDevice &) = delete;

  // One line per command recorded since beginFrame, e.g.
  // `barrier "scene color" ColorAttachment->ShaderReadOnly`.
  [[nodiscard]] const std::vector<std::string> &trace() const noexcept {
    return m_trace;
  }
  // Live pipelines and buffers, for tests that check an owner destroys what it creates.
  [[nodiscard]] std::size_t pipelineCount() const noexcept {
    return m_pipelines.size();
  }
  [[nodiscard]] std::size_t bufferCount() const noexcept {
    return m_buffers.size();
  }

  const DeviceInfo &info() const override {
    return m_info;
  }
  std::unique_ptr<ISwapchain> createSwapchain(platform::IWindow &window) override;

  BufferHandle createBuffer(const BufferDesc &desc) override;
  void destroyBuffer(BufferHandle handle) override;
  std::span<std::byte> mappedRange(BufferHandle handle) override;
  std::uint64_t bufferAddress(BufferHandle handle) const override;

  ImageHandle createImage(const ImageDesc &desc) override;
  void destroyImage(ImageHandle handle) override;
  const ImageDesc &imageDesc(ImageHandle handle) const override;
  std::uint32_t sampledImageIndex(ImageHandle handle) const override;
  std::uint32_t storageImageIndex(ImageHandle handle, std::uint32_t mipLevel) override;

  SamplerHandle createSampler(const SamplerDesc &desc) override;
  void destroySampler(SamplerHandle handle) override;
  std::uint32_t samplerIndex(SamplerHandle handle) const override;

  // Traced as `uploadBuffer "name" N bytes at offset` and `uploadImage "name" level L layer K N
  // bytes`; a host-visible buffer also receives the data.
  void uploadBuffer(BufferHandle handle, std::uint64_t offset, std::span<const std::byte> data) override;
  void uploadImage(ImageHandle handle, std::span<const ImageUpload> uploads) override;

  ShaderHandle createShader(const ShaderDesc &desc) override;
  void destroyShader(ShaderHandle handle) override;
  PipelineHandle createGraphicsPipeline(const GraphicsPipelineDesc &desc) override;
  PipelineHandle createComputePipeline(const ComputePipelineDesc &desc) override;
  void destroyPipeline(PipelineHandle handle) override;

  bool isValid(BufferHandle handle) const override;
  bool isValid(ImageHandle handle) const override;
  bool isValid(SamplerHandle handle) const override;
  bool isValid(ShaderHandle handle) const override;
  bool isValid(PipelineHandle handle) const override;

  ICommandList &beginFrame() override;
  void endFrame() override;
  void waitIdle() override;

  TransientAllocation allocateTransient(std::uint64_t size) override;
  std::span<const std::uint64_t> timestamps() const override;
  MemoryBudget memoryBudget() const override;

  std::uint32_t validationMessageCount() const override {
    return 0;
  }

private:
  friend class NullCommandList;

  struct Buffer {
    BufferDesc desc;
    std::vector<std::byte> memory; // host-visible buffers only
    std::uint64_t address{0};
  };
  struct Image {
    ImageDesc desc;
    std::uint32_t sampledIndex{InvalidBindlessIndex};
    std::vector<std::uint32_t> storageIndices;
  };
  struct Sampler {
    SamplerDesc desc;
    std::uint32_t index{InvalidBindlessIndex};
  };
  struct Shader {
    std::string debugName;
  };
  struct Pipeline {
    std::string debugName;
    bool compute{false};
  };
  struct Frame {
    BufferHandle transientBuffer;
    std::uint64_t transientOffset{0};
    std::uint32_t timestampCount{0};
    std::vector<std::uint64_t> timestampResults;
  };

  [[nodiscard]] std::string imageName(ImageHandle handle) const;
  [[nodiscard]] std::string bufferName(BufferHandle handle) const;

  DeviceInfo m_info;
  std::vector<std::string> m_trace;
  std::unique_ptr<NullCommandList> m_commandList;
  std::array<Frame, FramesInFlight> m_frames;
  std::uint32_t m_frameIndex{0};
  bool m_recording{false};
  std::uint64_t m_nextAddress{0x1000};
  // Bindless slots are handed out in creation order and never reused, which keeps traces stable.
  std::uint32_t m_nextSampledIndex{0};
  std::uint32_t m_nextCubeIndex{0};
  std::uint32_t m_nextStorageIndex{0};
  std::uint32_t m_nextSamplerIndex{0};
  std::uint32_t m_nextComparisonSamplerIndex{0};
  core::HandlePool<Buffer, BufferTag> m_buffers;
  core::HandlePool<Image, ImageTag> m_images;
  core::HandlePool<Sampler, SamplerTag> m_samplers;
  core::HandlePool<Shader, ShaderTag> m_shaders;
  core::HandlePool<Pipeline, PipelineTag> m_pipelines;
};

[[nodiscard]] std::unique_ptr<NullDevice> createNullDevice();

} // namespace sonnet::rhi
