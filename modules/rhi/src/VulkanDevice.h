#pragma once

#include "VulkanCommandList.h"
#include "VulkanResources.h"

#include <sonnet/rhi/Device.h>

#include <sonnet/core/HandlePool.h>

#include <vulkan-memory-allocator-hpp/vk_mem_alloc.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>
#include <vector>

namespace sonnet::rhi {

class VulkanSwapchain;

class VulkanDevice final : public IDevice {
public:
  explicit VulkanDevice(const DeviceDesc &desc);
  ~VulkanDevice() override;
  VulkanDevice(const VulkanDevice &) = delete;
  VulkanDevice &operator=(const VulkanDevice &) = delete;

  const DeviceInfo &info() const override {
    return m_info;
  }

  std::unique_ptr<ISwapchain> createSwapchain(platform::IWindow &window) override;

  BufferHandle createBuffer(const BufferDesc &desc) override;
  void destroyBuffer(BufferHandle handle) override;
  std::span<std::byte> mappedRange(BufferHandle handle) override;

  ImageHandle createImage(const ImageDesc &desc) override;
  void destroyImage(ImageHandle handle) override;
  const ImageDesc &imageDesc(ImageHandle handle) const override;

  ShaderHandle createShader(const ShaderDesc &desc) override;
  void destroyShader(ShaderHandle handle) override;
  PipelineHandle createGraphicsPipeline(const GraphicsPipelineDesc &desc) override;
  void destroyPipeline(PipelineHandle handle) override;

  bool isValid(BufferHandle handle) const override;
  bool isValid(ImageHandle handle) const override;
  bool isValid(ShaderHandle handle) const override;
  bool isValid(PipelineHandle handle) const override;

  ICommandList &beginFrame() override;
  void endFrame() override;
  void waitIdle() override;

  std::uint32_t validationMessageCount() const override {
    return m_validationMessages.load();
  }

  // Internal API for the swapchain and command list.
  const vk::raii::Instance &instance() const noexcept {
    return m_instance;
  }
  const vk::raii::PhysicalDevice &physicalDevice() const noexcept {
    return m_physicalDevice;
  }
  const vk::raii::Device &device() const noexcept {
    return m_device;
  }
  std::uint32_t graphicsFamily() const noexcept {
    return m_graphicsFamily;
  }
  const VulkanBuffer *findBuffer(BufferHandle handle) const noexcept {
    return m_buffers.find(handle);
  }
  const VulkanImage *findImage(ImageHandle handle) const noexcept {
    return m_images.find(handle);
  }
  const VulkanPipeline *findPipeline(PipelineHandle handle) const noexcept {
    return m_pipelines.find(handle);
  }
  vk::PipelineLayout pipelineLayout() const noexcept {
    return *m_pipelineLayout;
  }
  // Registers an image the device does not own (swapchain images); release with destroyImage.
  ImageHandle registerExternalImage(vk::Image image, const ImageDesc &desc);
  // The swapchain acquired an image this frame: wait on its semaphore, present it at endFrame.
  void addPendingPresent(VulkanSwapchain &swapchain, std::uint32_t imageIndex, vk::Semaphore imageAvailable);
  // Set once per frame by the swapchain from the per-frame slot.
  vk::Semaphore currentImageAvailableSemaphore() const noexcept {
    return *m_frames[m_frameIndex].imageAvailable;
  }
  void setDebugName(vk::ObjectType type, std::uint64_t handle, std::string_view name) const;

  void onDebugMessage(vk::DebugUtilsMessageSeverityFlagBitsEXT severity, vk::DebugUtilsMessageTypeFlagsEXT type,
                      const vk::DebugUtilsMessengerCallbackDataEXT &data);

private:
  struct Frame {
    vk::raii::CommandPool commandPool{nullptr};
    vk::raii::CommandBuffer commandBuffer{nullptr};
    vk::raii::Semaphore imageAvailable{nullptr};
    std::uint64_t submittedValue{0};
    // Resources released while this frame was recording; freed once the GPU is past it.
    std::vector<std::function<void()>> garbage;
  };

  struct PendingPresent {
    VulkanSwapchain *swapchain;
    std::uint32_t imageIndex;
    vk::Semaphore imageAvailable;
  };

  void createInstance(const DeviceDesc &desc);
  void selectAndCreateDevice(const DeviceDesc &desc);
  void createAllocator();
  void createPipelineLayout();
  void createFrames();
  void waitForFrame(Frame &frame);
  void deferDestruction(std::function<void()> destroy);
  void reportLeaks();

  DeviceInfo m_info;
  std::atomic<std::uint32_t> m_validationMessages{0};

  // Reverse destruction order: everything below is destroyed bottom-up (ADR-0006).
  vk::raii::Context m_context;
  vk::raii::Instance m_instance{nullptr};
  vk::raii::DebugUtilsMessengerEXT m_messenger{nullptr};
  vk::raii::PhysicalDevice m_physicalDevice{nullptr};
  vk::raii::Device m_device{nullptr};
  std::uint32_t m_graphicsFamily{0};
  vk::raii::Queue m_graphicsQueue{nullptr};
  vma::UniqueAllocator m_allocator;
  vk::raii::PipelineLayout m_pipelineLayout{nullptr};
  vk::raii::Semaphore m_timeline{nullptr};
  std::uint64_t m_timelineValue{0};
  std::array<Frame, FramesInFlight> m_frames;
  std::uint32_t m_frameIndex{0};
  bool m_recording{false};
  std::vector<PendingPresent> m_pendingPresents;
  VulkanCommandList m_commandList;

  core::HandlePool<VulkanBuffer, BufferTag> m_buffers;
  core::HandlePool<VulkanImage, ImageTag> m_images;
  core::HandlePool<VulkanShader, ShaderTag> m_shaders;
  core::HandlePool<VulkanPipeline, PipelineTag> m_pipelines;
};

} // namespace sonnet::rhi
