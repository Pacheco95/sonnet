#pragma once

#include <sonnet/rhi/CommandList.h>
#include <sonnet/rhi/Device.h>
#include <sonnet/rhi/Types.h>

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace sonnet::renderer {

// An image known to the graph for one frame: transient (owned by the graph's pool) or imported.
struct GraphImage {
  std::uint32_t index{0xFFFFFFFFu};

  [[nodiscard]] constexpr bool isValid() const noexcept {
    return index != 0xFFFFFFFFu;
  }
};

// How a pass uses an image. The graph derives the layout, stage and access from it.
enum class ImageAccess : std::uint8_t {
  ColorAttachment,
  DepthAttachment,
  Sampled,
  TransferSrc,
  TransferDst,
};

// Resolves graph images to rhi handles inside a pass.
class PassResources {
public:
  [[nodiscard]] rhi::ImageHandle image(GraphImage image) const;

private:
  friend class RenderGraph;
  explicit PassResources(const std::vector<rhi::ImageHandle> &handles) : m_handles(handles) {
  }
  const std::vector<rhi::ImageHandle> &m_handles;
};

using PassExecute = std::function<void(rhi::ICommandList &, const PassResources &)>;

namespace detail {

struct ImageUse {
  GraphImage image;
  ImageAccess access{ImageAccess::Sampled};
};

struct Pass {
  std::string name;
  std::vector<ImageUse> uses;
  std::vector<rhi::ColorAttachment> colors; // image fields hold graph indices until execute
  rhi::DepthAttachment depth;
  bool hasDepth{false};
  PassExecute execute;
};

} // namespace detail

// Declares what a pass touches. A pass with attachments is a graphics pass: the graph opens
// dynamic rendering on them, in declaration order, around execute.
class PassBuilder {
public:
  void color(GraphImage image, rhi::LoadOp load = rhi::LoadOp::Clear, glm::vec4 clear = {0.0f, 0.0f, 0.0f, 1.0f},
             rhi::StoreOp store = rhi::StoreOp::Store);
  void depth(GraphImage image, rhi::LoadOp load = rhi::LoadOp::Clear, float clear = 0.0f,
             rhi::StoreOp store = rhi::StoreOp::Store);
  void sample(GraphImage image);
  void transferSrc(GraphImage image);
  void transferDst(GraphImage image);

private:
  friend class RenderGraph;
  explicit PassBuilder(detail::Pass &pass) : m_pass(pass) {
  }
  detail::Pass &m_pass;
};

struct PassTiming {
  std::string name;
  float cpuMilliseconds{0.0f}; // recording time on the CPU
  float gpuMilliseconds{0.0f}; // from the timestamps of the frame that last used this slot
};

struct GraphStatistics {
  std::vector<PassTiming> passes;
  std::uint32_t barrierCount{0};
  std::uint32_t transientImageCount{0}; // images alive in the pool
  std::uint64_t transientImageBytes{0};
};

// Frame graph: passes declare the images they read and write; execute allocates transient
// images from a pool, emits the barriers between passes in declaration order, brackets each
// pass with timestamps, and transitions imported images to their requested final layout. Built
// fresh every frame between reset and execute (docs/rendering.md, "Render graph").
class RenderGraph {
public:
  explicit RenderGraph(rhi::IDevice &device);
  ~RenderGraph();
  RenderGraph(const RenderGraph &) = delete;
  RenderGraph &operator=(const RenderGraph &) = delete;

  // Forgets the previous frame's passes and images; pooled transient images stay allocated.
  void reset();

  // An image the caller owns. Its contents are treated as undefined at the start of the frame:
  // the first pass must write it. finalLayout Undefined leaves it in the state of its last use.
  GraphImage importImage(rhi::ImageHandle image, rhi::ImageLayout finalLayout = rhi::ImageLayout::Undefined);
  // A transient image for this frame. Usage bits are added from the passes that use it.
  GraphImage createImage(const rhi::ImageDesc &desc);

  // setup runs immediately and declares the pass's resources; execute runs during execute().
  // The callbacks are std::function: their captures are a few references, within the small
  // buffer, so no allocation happens per frame.
  void addPass(std::string_view name, const std::function<void(PassBuilder &)> &setup, PassExecute execute);

  // Records the frame into the command list obtained from IDevice::beginFrame.
  void execute(rhi::ICommandList &commands);

  [[nodiscard]] const GraphStatistics &statistics() const noexcept {
    return m_statistics;
  }

private:
  struct Image {
    rhi::ImageDesc desc; // transient only
    rhi::ImageHandle imported;
    rhi::ImageLayout finalLayout{rhi::ImageLayout::Undefined};
    bool transient{false};
  };
  struct PooledImage {
    rhi::ImageDesc desc;
    rhi::ImageHandle handle;
    bool inUse{false};
    std::uint64_t lastUsedFrame{0};
  };
  struct State {
    rhi::ImageLayout layout{rhi::ImageLayout::Undefined};
    rhi::PipelineStage stage{rhi::PipelineStage::AllCommands};
    rhi::Access access{rhi::Access::None};
  };

  void resolveImages();
  void releaseImages();
  void emitBarriers(rhi::ICommandList &commands, const std::vector<detail::ImageUse> &uses);
  void readTimings();

  rhi::IDevice &m_device;
  std::vector<Image> m_images;
  std::vector<rhi::ImageHandle> m_handles;
  std::vector<State> m_states;
  std::vector<detail::Pass> m_passes;
  std::vector<PooledImage> m_pool;
  std::uint64_t m_frameCounter{0};
  // Pass names per frame slot, to pair timestamps with the passes that wrote them.
  std::array<std::vector<std::string>, rhi::FramesInFlight> m_slotPassNames;
  GraphStatistics m_statistics;
};

} // namespace sonnet::renderer
