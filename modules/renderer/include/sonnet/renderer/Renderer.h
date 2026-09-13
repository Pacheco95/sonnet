#pragma once

#include <sonnet/renderer/Mesh.h>
#include <sonnet/renderer/RenderGraph.h>
#include <sonnet/renderer/SceneView.h>

#include <sonnet/core/HandlePool.h>
#include <sonnet/rhi/Device.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace sonnet::renderer {

struct RenderStatistics {
  std::uint32_t drawCount{0};
  std::uint32_t triangleCount{0};
};

// Owns meshes and the engine pipelines, and adds the scene passes to a render graph. Creation
// throws when a shader is missing or rejected; per-frame calls never throw.
class Renderer {
public:
  // The formats every scene target uses; RenderTarget creates them and the pipelines match.
  static constexpr rhi::Format ColorFormat = rhi::Format::R8G8B8A8Unorm;
  static constexpr rhi::Format DepthFormat = rhi::Format::D32Sfloat;
  static constexpr rhi::Format IdFormat = rhi::Format::R32Uint;
  // Outlined ids per frame; the push constants hold them (shaders/outline.slang).
  static constexpr std::size_t MaxOutlineIds = 16;

  // shaderDir holds the modules compiled by sonnet_add_engine_shaders (`forward.spv`, ...).
  Renderer(rhi::IDevice &device, const std::filesystem::path &shaderDir);
  ~Renderer();
  Renderer(const Renderer &) = delete;
  Renderer &operator=(const Renderer &) = delete;

  // Uploads the mesh; until the staging ring arrives in M3 the buffers are host-visible.
  [[nodiscard]] MeshHandle createMesh(const MeshData &data, std::string debugName);
  void destroyMesh(MeshHandle handle);
  [[nodiscard]] bool isValid(MeshHandle handle) const;

  // Declares the passes that draw `view` into `color` and `depth`, both cleared. `view` and the
  // draw list it spans must outlive the graph's execute.
  void addScenePasses(RenderGraph &graph, const SceneView &view, GraphImage color, GraphImage depth,
                      glm::vec4 clearColor = {0.05f, 0.05f, 0.07f, 1.0f});
  // Declares the id pass: every item's id into `ids` (IdFormat, cleared to 0), tested against
  // the depth the scene passes wrote so only visible surfaces remain. Same lifetime rule.
  void addIdPass(RenderGraph &graph, const SceneView &view, GraphImage ids, GraphImage depth);
  // Declares the outline pass over `color`, drawing `outlineColor` next to the silhouettes of
  // the entities in `ids` whose id is listed, up to MaxOutlineIds (the rest are ignored).
  void addOutlinePass(RenderGraph &graph, GraphImage color, GraphImage ids, std::span<const std::uint32_t> selected,
                      glm::vec4 outlineColor = {1.0f, 0.6f, 0.1f, 1.0f});

  [[nodiscard]] const RenderStatistics &statistics() const noexcept {
    return m_statistics;
  }

private:
  struct Mesh {
    std::string debugName;
    rhi::BufferHandle vertices;
    rhi::BufferHandle indices;
    std::uint32_t indexCount{0};
  };
  struct PassBuffers {
    rhi::TransientAllocation frame;
    rhi::TransientAllocation objects;
    [[nodiscard]] bool valid() const noexcept {
      return !frame.data.empty() && !objects.data.empty();
    }
  };

  [[nodiscard]] rhi::PipelineHandle createPipeline(const std::filesystem::path &shaderDir, const char *name,
                                                   rhi::GraphicsPipelineDesc desc);
  [[nodiscard]] PassBuffers uploadPassBuffers(const SceneView &view, glm::uvec2 targetSize);
  void recordDraws(rhi::ICommandList &commands, const SceneView &view, const PassBuffers &buffers,
                   rhi::PipelineHandle pipeline, bool count);
  void recordForward(rhi::ICommandList &commands, const SceneView &view, glm::uvec2 targetSize);
  void recordIds(rhi::ICommandList &commands, const SceneView &view, glm::uvec2 targetSize);
  void recordOutline(rhi::ICommandList &commands, rhi::ImageHandle ids);

  rhi::IDevice &m_device;
  rhi::PipelineHandle m_forwardPipeline;
  rhi::PipelineHandle m_idPipeline;
  rhi::PipelineHandle m_outlinePipeline;
  core::HandlePool<Mesh, MeshTag> m_meshes;
  RenderStatistics m_statistics;
  std::array<std::uint32_t, MaxOutlineIds> m_outlineIds{};
  std::uint32_t m_outlineCount{0};
  glm::vec4 m_outlineColor{1.0f, 0.6f, 0.1f, 1.0f};
};

} // namespace sonnet::renderer
