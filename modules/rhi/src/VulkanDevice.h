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
#include <thread>
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
  std::uint64_t bufferAddress(BufferHandle handle) const override;

  ImageHandle createImage(const ImageDesc &desc) override;
  void destroyImage(ImageHandle handle) override;
  const ImageDesc &imageDesc(ImageHandle handle) const override;
  std::uint32_t sampledImageIndex(ImageHandle handle) const override;
  std::uint32_t storageImageIndex(ImageHandle handle, std::uint32_t mipLevel) override;
  std::uint32_t storageBufferIndex(BufferHandle handle) override;

  SamplerHandle createSampler(const SamplerDesc &desc) override;
  void destroySampler(SamplerHandle handle) override;
  std::uint32_t samplerIndex(SamplerHandle handle) const override;

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
    return m_validationMessages.load();
  }

  // Internal API for the swapchain, the command list and the ui module's ImGui backend.
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
  const vk::raii::Queue &graphicsQueue() const noexcept {
    return m_graphicsQueue;
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
  vk::DescriptorSet bindlessSet() const noexcept {
    return *m_bindlessSet;
  }
  // Registers an image the device does not own (swapchain images); release with destroyImage.
  ImageHandle registerExternalImage(vk::Image image, const ImageDesc &desc);
  // The swapchain acquired an image this frame: wait on its semaphore, present it at endFrame.
  void addPendingPresent(VulkanSwapchain &swapchain, std::uint32_t imageIndex, vk::Semaphore imageAvailable);
  // Set once per frame by the swapchain from the per-frame slot.
  vk::Semaphore currentImageAvailableSemaphore() const noexcept {
    return *m_frames[m_frameIndex].imageAvailable;
  }
  // The recording frame's timestamp pool, or null when timestamps are unsupported.
  vk::QueryPool currentQueryPool() const noexcept {
    return m_info.timestampsSupported ? *m_frames[m_frameIndex].queryPool : vk::QueryPool{};
  }
  void noteTimestamp(std::uint32_t index) noexcept;
  void setDebugName(vk::ObjectType type, std::uint64_t handle, std::string_view name) const;

  void onDebugMessage(vk::DebugUtilsMessageSeverityFlagBitsEXT severity, vk::DebugUtilsMessageTypeFlagsEXT type,
                      const vk::DebugUtilsMessengerCallbackDataEXT &data);

private:
  // Free-list allocator for one bindless array.
  struct IndexAllocator {
    explicit IndexAllocator(std::uint32_t size) : capacity(size) {
    }
    std::uint32_t capacity{0};
    std::uint32_t next{0};
    std::vector<std::uint32_t> free;

    [[nodiscard]] std::uint32_t allocate(std::string_view what);
    void release(std::uint32_t index);
  };

  struct Frame {
    vk::raii::CommandPool commandPool{nullptr};
    vk::raii::CommandBuffer commandBuffer{nullptr};
    // Uploads recorded for this slot run before the frame's commands, in the same submission.
    vk::raii::CommandPool uploadPool{nullptr};
    vk::raii::CommandBuffer uploadCommandBuffer{nullptr};
    vk::raii::Semaphore imageAvailable{nullptr};
    vk::raii::QueryPool queryPool{nullptr};
    std::uint64_t submittedValue{0};
    // Resources released while this frame was recording; freed once the GPU is past it.
    std::vector<std::function<void()>> garbage;
    BufferHandle transientBuffer;
    std::uint64_t transientOffset{0};
    bool transientExhausted{false};
    BufferHandle stagingBuffer;
    std::uint64_t stagingOffset{0};
    bool uploadsRecorded{false};
    bool prepared{false};            // waited for, garbage freed, pools reset: ready for this slot's use
    std::uint32_t timestampCount{0}; // highest index written plus one
    std::vector<std::uint64_t> timestampResults;
  };

  struct PendingPresent {
    VulkanSwapchain *swapchain;
    std::uint32_t imageIndex;
    vk::Semaphore imageAvailable;
  };

  // A slice of staging memory for one upload: the slot's ring, or a dedicated buffer when the
  // data does not fit, released with the slot.
  struct Staging {
    vk::Buffer buffer;
    std::uint64_t offset{0};
    std::byte *mapped{nullptr};
  };

  void createInstance(const DeviceDesc &desc);
  void selectAndCreateDevice(const DeviceDesc &desc);
  void createAllocator();
  void createPipelineLayout();
  void createBindlessSet();
  void createFrames();
  void prepareSlot();
  vk::CommandBuffer uploadCommands();
  Staging stage(std::uint64_t size, std::string_view what);
  void writeSampledDescriptor(std::uint32_t binding, std::uint32_t index, vk::ImageView view, vk::ImageLayout layout);
  void writeSamplerDescriptor(std::uint32_t binding, std::uint32_t index, vk::Sampler sampler);
  void waitForFrame(Frame &frame);
  // Submits uploads recorded into the current slot that no endFrame has taken, so nothing is
  // left referencing a resource the caller is about to destroy. Only outside a frame.
  void submitRecordedUploads();
  void readTimestamps(Frame &frame);
  void deferDestruction(std::function<void()> destroy);
  void reportLeaks();
  // Asserts the caller is on the thread this device was created on; see OwnerThread.h.
  void assertOwnerThread(std::string_view what) const;

  DeviceInfo m_info;
  const std::thread::id m_ownerThread{std::this_thread::get_id()};
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
  vk::raii::DescriptorSetLayout m_bindlessLayout{nullptr};
  vk::raii::DescriptorSetLayout m_passLayout{nullptr};
  vk::raii::PipelineLayout m_pipelineLayout{nullptr};
  vk::raii::DescriptorPool m_bindlessPool{nullptr};
  vk::raii::DescriptorSet m_bindlessSet{nullptr};
  IndexAllocator m_sampledIndices{MaxBindlessSampledImages};
  IndexAllocator m_samplerIndices{MaxBindlessSamplers};
  IndexAllocator m_storageIndices{MaxBindlessStorageImages};
  IndexAllocator m_cubeIndices{MaxBindlessCubeImages};
  IndexAllocator m_comparisonSamplerIndices{MaxBindlessComparisonSamplers};
  IndexAllocator m_storageBufferIndices{MaxBindlessStorageBuffers};
  vk::raii::Semaphore m_timeline{nullptr};
  std::uint64_t m_timelineValue{0};
  std::uint64_t m_transientAlignment{256};
  float m_timestampPeriod{1.0f};
  float m_maxAnisotropy{1.0f};
  std::array<Frame, FramesInFlight> m_frames;
  std::uint32_t m_frameIndex{0};
  bool m_recording{false};
  std::vector<PendingPresent> m_pendingPresents;
  VulkanCommandList m_commandList;

  core::HandlePool<VulkanBuffer, BufferTag> m_buffers;
  core::HandlePool<VulkanImage, ImageTag> m_images;
  core::HandlePool<VulkanSampler, SamplerTag> m_samplers;
  core::HandlePool<VulkanShader, ShaderTag> m_shaders;
  core::HandlePool<VulkanPipeline, PipelineTag> m_pipelines;
};

} // namespace sonnet::rhi
