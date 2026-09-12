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
};

struct DeviceInfo {
  std::string deviceName;
  std::uint32_t apiVersion{0}; // packed Vulkan version
  bool validationEnabled{false};
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

  [[nodiscard]] virtual ImageHandle createImage(const ImageDesc &desc) = 0;
  virtual void destroyImage(ImageHandle handle) = 0;
  [[nodiscard]] virtual const ImageDesc &imageDesc(ImageHandle handle) const = 0;

  // Throws core::Exception when the SPIR-V is rejected by the driver.
  [[nodiscard]] virtual ShaderHandle createShader(const ShaderDesc &desc) = 0;
  virtual void destroyShader(ShaderHandle handle) = 0;

  // The shader may be destroyed once the pipeline exists.
  [[nodiscard]] virtual PipelineHandle createGraphicsPipeline(const GraphicsPipelineDesc &desc) = 0;
  virtual void destroyPipeline(PipelineHandle handle) = 0;

  [[nodiscard]] virtual bool isValid(BufferHandle handle) const = 0;
  [[nodiscard]] virtual bool isValid(ImageHandle handle) const = 0;
  [[nodiscard]] virtual bool isValid(ShaderHandle handle) const = 0;
  [[nodiscard]] virtual bool isValid(PipelineHandle handle) const = 0;

  // Waits for the frame slot's previous work, releases resources destroyed during that frame,
  // and starts recording.
  [[nodiscard]] virtual ICommandList &beginFrame() = 0;
  // Submits the frame and presents every swapchain image acquired since beginFrame.
  virtual void endFrame() = 0;
  virtual void waitIdle() = 0;

  // Validation errors and warnings seen since creation. Tests require zero.
  [[nodiscard]] virtual std::uint32_t validationMessageCount() const = 0;
};

// The one #if-switched site in engine code: picks the implementation selected by SONNET_RHI.
[[nodiscard]] std::unique_ptr<IDevice> createDevice(const DeviceDesc &desc);

} // namespace sonnet::rhi
