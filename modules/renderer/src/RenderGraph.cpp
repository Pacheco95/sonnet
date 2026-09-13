#include <sonnet/renderer/RenderGraph.h>

#include <sonnet/core/Assert.h>
#include <sonnet/core/Log.h>
#include <sonnet/core/Profile.h>

#include <algorithm>
#include <chrono>
#include <utility>

namespace sonnet::renderer {

namespace {

// Pooled transient images that no frame has used for this long are released.
constexpr std::uint64_t PoolRetentionFrames = rhi::FramesInFlight + 2;

struct Required {
  rhi::ImageLayout layout;
  rhi::PipelineStage stage;
  rhi::Access access;
  rhi::ImageUsage usage;
};

// `storageImage` is set for images whose bindless descriptors name the General layout.
Required requirementFor(ImageAccess access, bool storageImage) noexcept {
  using namespace rhi;
  const ImageLayout sampledLayout = storageImage ? ImageLayout::General : ImageLayout::ShaderReadOnly;
  switch (access) {
  case ImageAccess::ColorAttachment:
    return {ImageLayout::ColorAttachment, PipelineStage::ColorAttachmentOutput,
            Access::ColorAttachmentRead | Access::ColorAttachmentWrite, ImageUsage::ColorAttachment};
  case ImageAccess::DepthAttachment:
    return {ImageLayout::DepthAttachment, PipelineStage::EarlyFragmentTests | PipelineStage::LateFragmentTests,
            Access::DepthAttachmentRead | Access::DepthAttachmentWrite, ImageUsage::DepthAttachment};
  case ImageAccess::Sampled:
    return {sampledLayout, PipelineStage::FragmentShader, Access::ShaderRead, ImageUsage::Sampled};
  case ImageAccess::SampledCompute:
    return {sampledLayout, PipelineStage::ComputeShader, Access::ShaderRead, ImageUsage::Sampled};
  case ImageAccess::Storage:
    return {ImageLayout::General, PipelineStage::ComputeShader, Access::ShaderRead | Access::ShaderWrite,
            ImageUsage::Storage};
  case ImageAccess::TransferSrc:
    return {ImageLayout::TransferSrc, PipelineStage::Transfer, Access::TransferRead, ImageUsage::TransferSrc};
  case ImageAccess::TransferDst:
    return {ImageLayout::TransferDst, PipelineStage::Transfer, Access::TransferWrite, ImageUsage::TransferDst};
  }
  return {ImageLayout::General, PipelineStage::AllCommands, Access::MemoryRead | Access::MemoryWrite, ImageUsage::None};
}

float milliseconds(std::chrono::steady_clock::duration duration) {
  return std::chrono::duration<float, std::milli>(duration).count();
}

} // namespace

rhi::ImageHandle PassResources::image(GraphImage image) const {
  SONNET_ASSERT(image.index < m_handles.size(), "graph image {} is not part of this frame", image.index);
  return m_handles[image.index];
}

void PassBuilder::color(GraphImage image, rhi::LoadOp load, glm::vec4 clear, rhi::StoreOp store) {
  m_pass.uses.push_back({image, ImageAccess::ColorAttachment});
  m_pass.colors.push_back(
      rhi::ColorAttachment{.image = {image.index, 0}, .load = load, .store = store, .clearColor = clear});
}

void PassBuilder::depth(GraphImage image, rhi::LoadOp load, float clear, rhi::StoreOp store) {
  SONNET_ASSERT(!m_pass.hasDepth, "pass \"{}\" declares two depth attachments", m_pass.name);
  m_pass.uses.push_back({image, ImageAccess::DepthAttachment});
  m_pass.depth = rhi::DepthAttachment{.image = {image.index, 0}, .load = load, .store = store, .clearDepth = clear};
  m_pass.hasDepth = true;
}

void PassBuilder::sample(GraphImage image, bool compute) {
  m_pass.uses.push_back({image, compute ? ImageAccess::SampledCompute : ImageAccess::Sampled});
}

void PassBuilder::storage(GraphImage image) {
  m_pass.uses.push_back({image, ImageAccess::Storage});
}

void PassBuilder::transferSrc(GraphImage image) {
  m_pass.uses.push_back({image, ImageAccess::TransferSrc});
}

void PassBuilder::transferDst(GraphImage image) {
  m_pass.uses.push_back({image, ImageAccess::TransferDst});
}

RenderGraph::RenderGraph(rhi::IDevice &device) : m_device(device) {
}

RenderGraph::~RenderGraph() {
  for (const PooledImage &pooled : m_pool) {
    m_device.destroyImage(pooled.handle);
  }
}

void RenderGraph::reset() {
  m_images.clear();
  m_handles.clear();
  m_states.clear();
  m_passes.clear();
}

GraphImage RenderGraph::importImage(rhi::ImageHandle image, rhi::ImageLayout finalLayout,
                                    rhi::ImageLayout initialLayout) {
  SONNET_ASSERT(m_device.isValid(image), "importing a stale image handle");
  // The description is kept for the usage bits, which decide the layout of sampled reads.
  m_images.push_back(Image{.desc = m_device.imageDesc(image),
                           .imported = image,
                           .initialLayout = initialLayout,
                           .finalLayout = finalLayout,
                           .transient = false});
  return GraphImage{static_cast<std::uint32_t>(m_images.size() - 1)};
}

GraphImage RenderGraph::createImage(const rhi::ImageDesc &desc) {
  m_images.push_back(Image{.desc = desc,
                           .imported = {},
                           .initialLayout = rhi::ImageLayout::Undefined,
                           .finalLayout = rhi::ImageLayout::Undefined,
                           .transient = true});
  return GraphImage{static_cast<std::uint32_t>(m_images.size() - 1)};
}

const rhi::ImageDesc &RenderGraph::imageDesc(GraphImage image) const {
  SONNET_ASSERT(image.index < m_images.size(), "graph image {} is not part of this frame", image.index);
  return m_images[image.index].desc;
}

void RenderGraph::addPass(std::string_view name, const std::function<void(PassBuilder &)> &setup, PassExecute execute) {
  detail::Pass &pass = m_passes.emplace_back();
  pass.name.assign(name);
  pass.execute = std::move(execute);
  PassBuilder builder{pass};
  setup(builder);
  for (const detail::ImageUse &use : pass.uses) {
    SONNET_ASSERT(use.image.index < m_images.size(), "pass \"{}\" uses an image that is not in the graph", pass.name);
    Image &image = m_images[use.image.index];
    if (image.transient) {
      image.desc.usage |= requirementFor(use.access, false).usage;
    }
  }
}

void RenderGraph::resolveImages() {
  m_handles.resize(m_images.size());
  // An image's first barrier of the frame orders it after whatever the previous frame did to it:
  // an imported target is written every frame, and a pooled image may have been anything.
  m_states.assign(m_images.size(), State{.layout = rhi::ImageLayout::Undefined,
                                         .stage = rhi::PipelineStage::AllCommands,
                                         .access = rhi::Access::MemoryRead | rhi::Access::MemoryWrite});
  for (std::size_t i = 0; i < m_images.size(); ++i) {
    Image &image = m_images[i];
    if (!image.transient) {
      m_handles[i] = image.imported;
      m_states[i].layout = image.initialLayout;
      continue;
    }
    if (image.desc.usage == rhi::ImageUsage::None) {
      m_handles[i] = {}; // declared, used by no pass this frame: nothing to allocate
      continue;
    }
    auto pooled = std::ranges::find_if(
        m_pool, [&](const PooledImage &candidate) { return !candidate.inUse && candidate.desc == image.desc; });
    if (pooled == m_pool.end()) {
      const rhi::ImageHandle handle = m_device.createImage(image.desc);
      SONNET_LOG_DEBUG("transient image \"{}\" {}x{} allocated", image.desc.debugName, image.desc.size.x,
                       image.desc.size.y);
      m_pool.push_back(PooledImage{image.desc, handle, false, 0});
      pooled = std::prev(m_pool.end());
    }
    pooled->inUse = true;
    pooled->lastUsedFrame = m_frameCounter;
    m_handles[i] = pooled->handle;
  }
}

void RenderGraph::releaseImages() {
  std::erase_if(m_pool, [&](PooledImage &pooled) {
    pooled.inUse = false;
    if (m_frameCounter - pooled.lastUsedFrame <= PoolRetentionFrames) {
      return false;
    }
    SONNET_LOG_DEBUG("transient image \"{}\" released", pooled.desc.debugName);
    m_device.destroyImage(pooled.handle);
    return true;
  });
  m_statistics.transientImageCount = static_cast<std::uint32_t>(m_pool.size());
  m_statistics.transientImageBytes = 0;
  for (const PooledImage &pooled : m_pool) {
    m_statistics.transientImageBytes += pooled.desc.byteSize();
  }
}

void RenderGraph::emitBarriers(rhi::ICommandList &commands, const std::vector<detail::ImageUse> &uses) {
  // Bounded by what one pass declares; the command list caps a call at 32.
  std::array<rhi::ImageBarrier, 32> barriers;
  std::size_t count = 0;
  for (const detail::ImageUse &use : uses) {
    const Required required =
        requirementFor(use.access, rhi::has(m_images[use.image.index].desc.usage, rhi::ImageUsage::Storage));
    State &state = m_states[use.image.index];
    // A read after a read in the same layout needs no barrier; everything else does, including
    // a write after a write in the same layout, which is a hazard between two passes.
    const bool needed = state.layout != required.layout || rhi::isWrite(state.access) || rhi::isWrite(required.access);
    if (!needed) {
      state.stage |= required.stage;
      state.access |= required.access;
      continue;
    }
    SONNET_ASSERT(count < barriers.size(), "a pass declares more than {} images", barriers.size());
    barriers[count++] = rhi::ImageBarrier{.image = m_handles[use.image.index],
                                          .srcStage = state.stage,
                                          .srcAccess = state.access,
                                          .oldLayout = state.layout,
                                          .dstStage = required.stage,
                                          .dstAccess = required.access,
                                          .newLayout = required.layout};
    state = State{required.layout, required.stage, required.access};
  }
  if (count > 0) {
    commands.barrier(std::span{barriers.data(), count});
    m_statistics.barrierCount += static_cast<std::uint32_t>(count);
  }
}

void RenderGraph::readTimings() {
  // The device returns the timestamps of the frame that last used the current slot; they belong
  // to the passes recorded then, matched by name and position.
  const std::span<const std::uint64_t> stamps = m_device.timestamps();
  const std::vector<std::string> &names = m_slotPassNames[m_frameCounter % rhi::FramesInFlight];
  for (std::size_t i = 0; i < m_passes.size(); ++i) {
    PassTiming &timing = m_statistics.passes[i];
    timing.gpuMilliseconds = 0.0f;
    if (i < names.size() && names[i] == m_passes[i].name && 2 * i + 1 < stamps.size() && stamps[2 * i] != 0 &&
        stamps[2 * i + 1] >= stamps[2 * i]) {
      timing.gpuMilliseconds = static_cast<float>(stamps[2 * i + 1] - stamps[2 * i]) * 1.0e-6f;
    }
  }
}

void RenderGraph::execute(rhi::ICommandList &commands) {
  SONNET_ZONE();
  SONNET_ASSERT(m_passes.size() * 2 <= rhi::MaxTimestamps, "{} passes exceed the timestamp budget", m_passes.size());
  resolveImages();
  m_statistics.barrierCount = 0;
  m_statistics.passes.resize(m_passes.size());
  readTimings();

  const PassResources resources{m_handles};
  for (std::size_t i = 0; i < m_passes.size(); ++i) {
    detail::Pass &pass = m_passes[i];
    const auto start = std::chrono::steady_clock::now();
    emitBarriers(commands, pass.uses);
    commands.writeTimestamp(static_cast<std::uint32_t>(2 * i));
    const bool graphics = !pass.colors.empty() || pass.hasDepth;
    if (graphics) {
      for (rhi::ColorAttachment &color : pass.colors) {
        color.image = m_handles[color.image.index];
      }
      if (pass.hasDepth) {
        pass.depth.image = m_handles[pass.depth.image.index];
      }
      commands.beginRendering({.colors = pass.colors, .depth = pass.hasDepth ? &pass.depth : nullptr});
    }
    if (pass.execute) {
      pass.execute(commands, resources);
    }
    if (graphics) {
      commands.endRendering();
    }
    commands.writeTimestamp(static_cast<std::uint32_t>(2 * i + 1));
    m_statistics.passes[i].name = pass.name;
    m_statistics.passes[i].cpuMilliseconds = milliseconds(std::chrono::steady_clock::now() - start);
  }

  // Imported images end the frame where their owner expects them (the swapchain in Present).
  std::array<rhi::ImageBarrier, 32> finals;
  std::size_t finalCount = 0;
  for (std::size_t i = 0; i < m_images.size(); ++i) {
    const Image &image = m_images[i];
    if (image.transient || image.finalLayout == rhi::ImageLayout::Undefined ||
        m_states[i].layout == image.finalLayout) {
      continue;
    }
    SONNET_ASSERT(finalCount < finals.size(), "more than {} imported images", finals.size());
    finals[finalCount++] = rhi::ImageBarrier{.image = m_handles[i],
                                             .srcStage = m_states[i].stage,
                                             .srcAccess = m_states[i].access,
                                             .oldLayout = m_states[i].layout,
                                             .dstStage = rhi::PipelineStage::None,
                                             .dstAccess = rhi::Access::None,
                                             .newLayout = image.finalLayout};
  }
  if (finalCount > 0) {
    commands.barrier(std::span{finals.data(), finalCount});
    m_statistics.barrierCount += static_cast<std::uint32_t>(finalCount);
  }

  std::vector<std::string> &names = m_slotPassNames[m_frameCounter % rhi::FramesInFlight];
  names.resize(m_passes.size());
  for (std::size_t i = 0; i < m_passes.size(); ++i) {
    names[i] = m_passes[i].name;
  }
  ++m_frameCounter;
  releaseImages();
}

} // namespace sonnet::renderer
