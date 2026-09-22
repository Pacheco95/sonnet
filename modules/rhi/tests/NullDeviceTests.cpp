#include <sonnet/rhi/NullDevice.h>

#include <sonnet/core/Error.h>
#include <sonnet/core/JobSystem.h>
#include <sonnet/platform/Platform.h>
#include <sonnet/platform/Window.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>

using namespace sonnet::rhi;

namespace {

bool traceContains(const NullDevice &device, std::string_view text) {
  return std::ranges::any_of(device.trace(), [&](const std::string &line) { return line.contains(text); });
}

} // namespace

TEST_CASE("null device hands out handles that behave like the real ones", "[rhi][null]") {
  const auto device = createNullDevice();
  const BufferHandle upload = device->createBuffer(
      {.size = 64, .usage = BufferUsage::Storage, .memory = MemoryUsage::CpuToGpu, .debugName = "upload"});
  REQUIRE(device->isValid(upload));
  REQUIRE(device->mappedRange(upload).size() == 64);
  REQUIRE(device->bufferAddress(upload) != 0);
  const ImageHandle image = device->createImage(
      {.size = {8, 4}, .format = Format::D32Sfloat, .usage = ImageUsage::DepthAttachment, .debugName = "depth"});
  REQUIRE(device->imageDesc(image).size == glm::uvec2{8, 4});
  device->destroyBuffer(upload);
  REQUIRE(!device->isValid(upload));
  REQUIRE(device->mappedRange(upload).empty());
  device->destroyImage(image);
  REQUIRE(!device->isValid(image));
}

TEST_CASE("null device records a readable trace per frame", "[rhi][null]") {
  const auto device = createNullDevice();
  const ImageHandle color = device->createImage(
      {.size = {16, 16}, .format = Format::R8G8B8A8Unorm, .usage = ImageUsage::ColorAttachment, .debugName = "scene"});
  const ShaderHandle shader = device->createShader({.spirv = {}, .debugName = "shader"});
  const PipelineHandle pipeline =
      device->createGraphicsPipeline({.shader = shader, .colorFormats = {Format::R8G8B8A8Unorm}, .debugName = "lit"});

  ICommandList &commands = device->beginFrame();
  const ImageBarrier barrier{.image = color,
                             .srcStage = PipelineStage::AllCommands,
                             .oldLayout = ImageLayout::Undefined,
                             .dstStage = PipelineStage::ColorAttachmentOutput,
                             .dstAccess = Access::ColorAttachmentWrite,
                             .newLayout = ImageLayout::ColorAttachment};
  commands.barrier({&barrier, 1});
  const ColorAttachment attachment{.image = color};
  commands.beginRendering({.colors = {&attachment, 1}});
  commands.bindPipeline(pipeline);
  const ImageBinding imageBinding{.binding = PassImageBinding, .image = color};
  commands.bindImages({&imageBinding, 1});
  commands.draw(3);
  commands.endRendering();
  commands.writeTimestamp(1);
  REQUIRE(traceContains(*device, "barrier \"scene\" Undefined->ColorAttachment"));
  REQUIRE(traceContains(*device, "beginRendering color \"scene\" clear"));
  REQUIRE(traceContains(*device, "bindPipeline \"lit\""));
  REQUIRE(traceContains(*device, "bindImage 2 \"scene\""));
  REQUIRE(traceContains(*device, "draw 3 x1"));
  device->endFrame();

  static_cast<void>(device->beginFrame());
  REQUIRE(device->trace().empty()); // a new frame starts a new trace
  device->endFrame();

  device->destroyPipeline(pipeline);
  device->destroyShader(shader);
  device->destroyImage(color);
}

TEST_CASE("null device transient allocations and timestamps follow the frame slots", "[rhi][null]") {
  const auto device = createNullDevice();
  ICommandList &commands = device->beginFrame();
  const TransientAllocation a = device->allocateTransient(16);
  const TransientAllocation b = device->allocateTransient(16);
  REQUIRE(a.data.size() == 16);
  REQUIRE(b.offset == 256);
  std::memset(a.data.data(), 1, a.data.size());
  commands.writeTimestamp(0);
  commands.writeTimestamp(2);
  device->endFrame();
  for (std::uint32_t i = 1; i < FramesInFlight; ++i) {
    static_cast<void>(device->beginFrame());
    REQUIRE(device->timestamps().empty());
    device->endFrame();
  }
  static_cast<void>(device->beginFrame());
  REQUIRE(device->timestamps().size() == 3);
  REQUIRE(device->allocateTransient(16).offset == 0);
  device->endFrame();
}

TEST_CASE("null swapchain cycles through images of the window's size", "[rhi][null]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<sonnet::platform::IWindow> window;
  try {
    // Windows are Vulkan-capable, which needs the loader; the Windows and macOS runners have none.
    window = platform.createWindow({.title = "null", .size = {64, 48}});
  } catch (const sonnet::core::Exception &e) {
    SKIP("no window on this machine: " << e.what());
  }
  const auto device = createNullDevice();
  const auto swapchain = device->createSwapchain(*window);
  REQUIRE(swapchain->extent() == glm::uvec2{64, 48});
  REQUIRE(swapchain->imageCount() == 3);
  std::array<std::uint32_t, 3> seen{};
  for (int frame = 0; frame < 3; ++frame) {
    static_cast<void>(device->beginFrame());
    const auto image = swapchain->acquire();
    REQUIRE(image.has_value());
    REQUIRE(device->isValid(image->image));
    seen[static_cast<std::size_t>(frame)] = image->index;
    device->endFrame();
  }
  REQUIRE(seen == std::array<std::uint32_t, 3>{0, 1, 2});
}

TEST_CASE("null device reports a memory budget from its live resources", "[rhi][null]") {
  const auto device = createNullDevice();
  const MemoryBudget before = device->memoryBudget();
  const ImageHandle image = device->createImage(
      {.size = {64, 64}, .format = Format::R8G8B8A8Unorm, .usage = ImageUsage::Sampled, .debugName = "texture"});
  const MemoryBudget after = device->memoryBudget();
  REQUIRE(after.heapCount == 1);
  REQUIRE(after.heaps[0].usage == before.heaps[0].usage + 64 * 64 * 4);
  device->destroyImage(image);
}

TEST_CASE("null device traces uploads, compute dispatches and bindless slots", "[rhi][null]") {
  const auto device = createNullDevice();
  const BufferHandle vertices = device->createBuffer(
      {.size = 64, .usage = BufferUsage::Storage | BufferUsage::TransferDst, .debugName = "vertices"});
  const ImageHandle texture = device->createImage({.size = {2, 2},
                                                   .format = Format::R8G8B8A8Unorm,
                                                   .usage = ImageUsage::Sampled | ImageUsage::TransferDst,
                                                   .mipLevels = 2,
                                                   .debugName = "texture"});
  const ImageHandle storage = device->createImage({.size = {8, 8},
                                                   .format = Format::R16G16B16A16Sfloat,
                                                   .usage = ImageUsage::Sampled | ImageUsage::Storage,
                                                   .debugName = "storage"});
  const SamplerHandle sampler = device->createSampler({.debugName = "linear"});
  const SamplerHandle shadow = device->createSampler({.compare = true, .debugName = "shadow"});
  REQUIRE(device->sampledImageIndex(texture) == 0);
  REQUIRE(device->sampledImageIndex(storage) == 1);
  REQUIRE(device->storageImageIndex(storage, 0) == 0);
  REQUIRE(device->storageImageIndex(texture, 0) == InvalidBindlessIndex);
  REQUIRE(device->samplerIndex(sampler) == 0);
  REQUIRE(device->samplerIndex(shadow) == 0);
  // Storage buffers take a vertex-pulling slot on first request, and keep it.
  REQUIRE(device->storageBufferIndex(vertices) == 0);
  REQUIRE(device->storageBufferIndex(vertices) == 0);
  const BufferHandle indices = device->createBuffer({.size = 64, .usage = BufferUsage::Index, .debugName = "indices"});
  REQUIRE(device->storageBufferIndex(indices) == InvalidBindlessIndex);
  device->destroyBuffer(indices);

  const ShaderHandle shader = device->createShader({.spirv = {}, .debugName = "shader"});
  const PipelineHandle fill = device->createComputePipeline({.shader = shader, .debugName = "fill"});

  ICommandList &commands = device->beginFrame();
  const std::array<std::byte, 16> data{};
  device->uploadBuffer(vertices, 32, data);
  const std::array<std::byte, 16> level0{};
  const std::array<std::byte, 4> level1{};
  const std::array uploads{ImageUpload{.mipLevel = 0, .data = level0}, ImageUpload{.mipLevel = 1, .data = level1}};
  device->uploadImage(texture, uploads);
  commands.bindPipeline(fill);
  commands.dispatch(4, 2, 1);
  commands.memoryBarrier({.srcStage = PipelineStage::ComputeShader,
                          .srcAccess = Access::ShaderWrite,
                          .dstStage = PipelineStage::FragmentShader,
                          .dstAccess = Access::ShaderRead});
  REQUIRE(traceContains(*device, "uploadBuffer \"vertices\" 16 bytes at 32"));
  REQUIRE(traceContains(*device, "uploadImage \"texture\" level 0 layer 0 16 bytes"));
  REQUIRE(traceContains(*device, "uploadImage \"texture\" level 1 layer 0 4 bytes"));
  REQUIRE(traceContains(*device, "bindPipeline \"fill\""));
  REQUIRE(traceContains(*device, "dispatch 4 2 1"));
  REQUIRE(traceContains(*device, "memoryBarrier"));
  device->endFrame();

  device->destroyPipeline(fill);
  device->destroyShader(shader);
  device->destroySampler(shadow);
  device->destroySampler(sampler);
  device->destroyImage(storage);
  device->destroyImage(texture);
  device->destroyBuffer(vertices);
}

// A device belongs to the thread that created it, not to the process's main thread
// (src/OwnerThread.h). A violation asserts and so cannot be tested in process; what this pins is
// the other half of the rule, that a device created away from the main thread is usable there,
// which is what `sonnet_cook` and any future loading thread of its own rely on.
TEST_CASE("a null device created on a worker belongs to that worker", "[rhi][null][thread]") {
  sonnet::core::JobSystem jobs{{.workerCount = 2}};
  bool ran = false;
  const sonnet::core::JobHandle job = jobs.schedule("device on a worker", [&] {
    const auto device = createNullDevice();
    const BufferHandle buffer = device->createBuffer(
        {.size = 16, .usage = BufferUsage::Storage, .memory = MemoryUsage::CpuToGpu, .debugName = "worker"});
    device->beginFrame();
    const TransientAllocation transient = device->allocateTransient(16);
    device->endFrame();
    device->destroyBuffer(buffer);
    ran = device->isValid(buffer) == false && transient.data.size() == 16;
  });
  jobs.wait(job);
  REQUIRE(ran);
}
