#pragma once

#include <sonnet/rhi/CommandList.h>
#include <sonnet/rhi/Swapchain.h>
#include <sonnet/rhi/Types.h>

#include <sonnet/platform/Platform.h>
#include <sonnet/platform/Window.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace sonnet::rhi {

struct DeviceDesc {
  platform::Platform *platform{nullptr};
  std::string applicationName{"Sonnet"};
  // Requests the validation layer when it is installed; absent layers are logged, not fatal.
  bool enableValidation{SONNET_ENABLE_VALIDATION != 0};
  // Leaves drawIndirectCount disabled even where the device has it, so the GPU tests on Lavapipe
  // exercise the path MoltenVK takes (ADR-0014). Nothing else sets it.
  bool disableDrawIndirectCount{false};
};

struct DeviceInfo {
  std::string deviceName;
  std::string driverName;         // e.g. "NVIDIA", "radv", "llvmpipe"
  std::string driverInfo;         // driver version string as the driver reports it
  std::uint32_t apiVersion{0};    // packed Vulkan version of the device
  std::uint32_t loaderVersion{0}; // packed Vulkan version of the loader in the process
  bool validationEnabled{false};
  bool timestampsSupported{false};
  bool blockCompressionSupported{false};  // the BC4, BC5 and BC7 formats; desktop GPUs and Lavapipe have them
  bool drawIndirectCountSupported{false}; // every desktop driver and Lavapipe; not MoltenVK (ADR-0014)
};

constexpr std::uint32_t FramesInFlight = 2;

// The render hardware interface. Creation throws core::Exception; per-frame calls never throw.
// Resources are addressed by generation-checked handles and destroyed deferred, once the frame
// that may still use them has finished on the GPU.
class IDevice {
public:
  virtual ~IDevice() = default;

  [[nodiscard]] virtual const DeviceInfo &info() const = 0;

  [[nodiscard]] virtual std::unique_ptr<ISwapchain> createSwapchain(platform::IWindow &window) = 0;

  [[nodiscard]] virtual BufferHandle createBuffer(const BufferDesc &desc) = 0;
  virtual void destroyBuffer(BufferHandle handle) = 0;
  // Persistently mapped range of a CpuToGpu or GpuToCpu buffer; empty for GpuOnly.
  [[nodiscard]] virtual std::span<std::byte> mappedRange(BufferHandle handle) = 0;
  // Device address of a Storage buffer, for vertex pulling through push constants; 0 otherwise.
  [[nodiscard]] virtual std::uint64_t bufferAddress(BufferHandle handle) const = 0;

  [[nodiscard]] virtual ImageHandle createImage(const ImageDesc &desc) = 0;
  virtual void destroyImage(ImageHandle handle) = 0;
  [[nodiscard]] virtual const ImageDesc &imageDesc(ImageHandle handle) const = 0;
  // The image's slot in the bindless sampled-image array, or in the cube array for a cube image
  // (docs/rendering.md, "Frame structure"); InvalidBindlessIndex without Sampled usage.
  [[nodiscard]] virtual std::uint32_t sampledImageIndex(ImageHandle handle) const = 0;
  // The slot of one mip level in the bindless storage-image array, for compute shaders to write;
  // the view is created on first request. InvalidBindlessIndex without Storage usage.
  [[nodiscard]] virtual std::uint32_t storageImageIndex(ImageHandle handle, std::uint32_t mipLevel) = 0;
  // The buffer's slot in the bindless storage-buffer array for vertex pulling (docs/rendering.md,
  // "Frame structure"); assigned on first call, released on destroyBuffer. InvalidBindlessIndex
  // for a buffer without Storage usage.
  [[nodiscard]] virtual std::uint32_t storageBufferIndex(BufferHandle handle) = 0;

  [[nodiscard]] virtual SamplerHandle createSampler(const SamplerDesc &desc) = 0;
  virtual void destroySampler(SamplerHandle handle) = 0;
  // The sampler's slot in the bindless sampler array, or in the comparison-sampler array for a
  // sampler with `compare`.
  [[nodiscard]] virtual std::uint32_t samplerIndex(SamplerHandle handle) const = 0;

  // Uploads through the staging ring, at any point of a frame or between frames. The data is
  // copied now; the copy runs on the GPU before the next frame's commands, so a resource
  // uploaded during a frame is complete for that frame's draws. The buffer needs TransferDst
  // usage. An image is uploaded once, right after creation, one ImageUpload per level and
  // layer, and is left in ImageLayout::ShaderReadOnly; the render graph imports it with that
  // initial layout. Oversized data falls back to a dedicated staging buffer.
  virtual void uploadBuffer(BufferHandle handle, std::uint64_t offset, std::span<const std::byte> data) = 0;
  virtual void uploadImage(ImageHandle handle, std::span<const ImageUpload> uploads) = 0;

  // Throws core::Exception when the SPIR-V is rejected by the driver.
  [[nodiscard]] virtual ShaderHandle createShader(const ShaderDesc &desc) = 0;
  virtual void destroyShader(ShaderHandle handle) = 0;

  // The shader may be destroyed once the pipeline exists.
  [[nodiscard]] virtual PipelineHandle createGraphicsPipeline(const GraphicsPipelineDesc &desc) = 0;
  [[nodiscard]] virtual PipelineHandle createComputePipeline(const ComputePipelineDesc &desc) = 0;
  virtual void destroyPipeline(PipelineHandle handle) = 0;

  [[nodiscard]] virtual bool isValid(BufferHandle handle) const = 0;
  [[nodiscard]] virtual bool isValid(ImageHandle handle) const = 0;
  [[nodiscard]] virtual bool isValid(SamplerHandle handle) const = 0;
  [[nodiscard]] virtual bool isValid(ShaderHandle handle) const = 0;
  [[nodiscard]] virtual bool isValid(PipelineHandle handle) const = 0;

  // Waits for the frame slot's previous work, releases resources destroyed during that frame,
  // and starts recording.
  [[nodiscard]] virtual ICommandList &beginFrame() = 0;
  // Submits the frame and presents every swapchain image acquired since beginFrame.
  virtual void endFrame() = 0;
  virtual void waitIdle() = 0;

  // Between beginFrame and endFrame: a slice of the frame's host-visible linear allocator, aligned
  // for uniform and storage binding, valid until the slot is reused. An exhausted allocator logs
  // an error and returns an empty span; the caller skips the work.
  [[nodiscard]] virtual TransientAllocation allocateTransient(std::uint64_t size) = 0;

  // Between beginFrame and endFrame: the timestamps written by the frame that last used this
  // slot, FramesInFlight frames ago, in nanoseconds. Slots never written read as zero; empty when
  // the previous frame in the slot wrote none or timestamps are unsupported.
  [[nodiscard]] virtual std::span<const std::uint64_t> timestamps() const = 0;

  [[nodiscard]] virtual MemoryBudget memoryBudget() const = 0;

  // Validation errors and warnings seen since creation. Tests require zero.
  [[nodiscard]] virtual std::uint32_t validationMessageCount() const = 0;
};

// The one #if-switched site in engine code: picks the implementation selected by SONNET_RHI.
[[nodiscard]] std::unique_ptr<IDevice> createDevice(const DeviceDesc &desc);

} // namespace sonnet::rhi
