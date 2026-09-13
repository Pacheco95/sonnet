#pragma once

#include <sonnet/renderer/Mesh.h>
#include <sonnet/renderer/RenderGraph.h>
#include <sonnet/renderer/SceneView.h>

#include <sonnet/core/HandlePool.h>
#include <sonnet/rhi/Device.h>

#include <cstdint>
#include <filesystem>
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

  void recordForward(rhi::ICommandList &commands, const SceneView &view, glm::uvec2 targetSize);

  rhi::IDevice &m_device;
  rhi::PipelineHandle m_forwardPipeline;
  core::HandlePool<Mesh, MeshTag> m_meshes;
  RenderStatistics m_statistics;
};

} // namespace sonnet::renderer
