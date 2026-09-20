#pragma once

#include <sonnet/renderer/Material.h>
#include <sonnet/renderer/Mesh.h>
#include <sonnet/renderer/RenderGraph.h>
#include <sonnet/renderer/SceneView.h>
#include <sonnet/renderer/Texture.h>

#include <sonnet/core/Error.h>
#include <sonnet/core/HandlePool.h>
#include <sonnet/rhi/Device.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace sonnet::renderer {

struct RenderStatistics {
  std::uint32_t drawCount{0}; // scene draws: opaque and blended, not the shadow, id or mask passes
  std::uint32_t triangleCount{0};
  std::uint32_t shadowDrawCount{0};
  std::uint32_t lightCount{0};
  std::uint32_t skinnedInstanceCount{0}; // instances the skinning pass deformed
  std::uint32_t skinnedVertexCount{0};
};

// Quality knobs. Tests turn the sizes and sample counts down so Lavapipe finishes quickly.
struct RendererSettings {
  bool shadows{true};
  std::uint32_t shadowMapSize{2048}; // per cascade
  float shadowDistance{80.0f};       // metres of view depth the cascades cover
  float shadowBias{0.0015f};         // in reversed-Z depth units, scaled by the cascade
  bool bloom{true};
  std::uint32_t bloomLevels{5};
  bool antialiasing{true};
  std::uint32_t environmentSize{512}; // the skybox cube, with a full mip chain
  std::uint32_t irradianceSize{32};
  std::uint32_t irradianceSamples{256};
  std::uint32_t prefilteredSize{128};
  std::uint32_t prefilteredLevels{5};
  std::uint32_t prefilterSamples{256};
  std::uint32_t brdfLutSize{256};
  std::uint32_t brdfLutSamples{256};
};

// Owns meshes, textures, materials, environments and the engine pipelines, and adds the scene
// passes to a render graph (docs/rendering.md, "The renderer module today"). Creation throws
// when a shader is missing or rejected; per-frame calls never throw.
class Renderer {
public:
  // The formats every scene target uses; RenderTarget creates them and the pipelines match.
  static constexpr rhi::Format ColorFormat = rhi::Format::R8G8B8A8Unorm;
  static constexpr rhi::Format DepthFormat = rhi::Format::D32Sfloat;
  static constexpr rhi::Format IdFormat = rhi::Format::R32Uint;
  static constexpr rhi::Format HdrFormat = rhi::Format::R16G16B16A16Sfloat;
  static constexpr std::uint32_t CascadeCount = 4;
  static constexpr std::uint32_t MaxLights = 1024;

  // shaderDir holds the modules compiled by sonnet_add_engine_shaders (`forward.spv`, ...).
  Renderer(rhi::IDevice &device, const std::filesystem::path &shaderDir, const RendererSettings &settings = {});
  ~Renderer();
  Renderer(const Renderer &) = delete;
  Renderer &operator=(const Renderer &) = delete;

  // Uploads the mesh into device-local buffers. Without submeshes the whole mesh is one. Skin
  // weights, when there is one per vertex, make the mesh deformable by skinned draws.
  [[nodiscard]] MeshHandle createMesh(const MeshData &data, std::string debugName);
  void destroyMesh(MeshHandle handle);
  [[nodiscard]] bool isValid(MeshHandle handle) const;
  [[nodiscard]] std::span<const Submesh> submeshes(MeshHandle handle) const;
  [[nodiscard]] Bounds meshBounds(MeshHandle handle) const;

  // Uploads every level and layer; data of the wrong size is an error and gives no texture.
  [[nodiscard]] TextureHandle createTexture(const TextureData &data, std::string debugName);
  void destroyTexture(TextureHandle handle);
  [[nodiscard]] bool isValid(TextureHandle handle) const;
  // The bindless index shaders read the texture through; the white default for an invalid handle.
  [[nodiscard]] std::uint32_t textureIndex(TextureHandle handle) const;

  [[nodiscard]] MaterialHandle createMaterial(const MaterialDesc &desc, std::string debugName);
  void updateMaterial(MaterialHandle handle, const MaterialDesc &desc);
  void destroyMaterial(MaterialHandle handle);
  [[nodiscard]] bool isValid(MaterialHandle handle) const;
  [[nodiscard]] const MaterialDesc &material(MaterialHandle handle) const;

  // An environment from an equirectangular map, typically RGBA16F: the skybox cube, the
  // irradiance cube and the prefiltered specular cube are computed by passes the next
  // addScenePasses declares (docs/rendering.md, "Image-based lighting").
  [[nodiscard]] EnvironmentHandle createEnvironment(const TextureData &equirectangular, std::string debugName);
  void destroyEnvironment(EnvironmentHandle handle);
  [[nodiscard]] bool isValid(EnvironmentHandle handle) const;
  // True once the precompute passes have been declared, from the frame after creation.
  [[nodiscard]] bool isReady(EnvironmentHandle handle) const;

  // Declares the passes that draw `view` into `color` and `depth` (docs/rendering.md, "Frame
  // structure"): the shadow cascades, the depth pre-pass, light clustering, the forward pass into
  // an HDR image, the skybox, the blended draws, bloom, tone mapping into `color` and FXAA, plus
  // any pending environment or lookup-table precomputation. `view` and the spans it holds must
  // outlive the graph's execute. The first pass the renderer declares in a graph frame, this or
  // the id or mask pass, is preceded by the skinning pass when the view has skinned draws.
  void addScenePasses(RenderGraph &graph, const SceneView &view, GraphImage color, GraphImage depth,
                      glm::vec4 clearColor = {0.05f, 0.05f, 0.07f, 1.0f});
  // Declares the id pass: every item's id into `ids` (IdFormat, cleared to 0), tested against
  // the depth the scene passes wrote so only visible surfaces remain. Same lifetime rule.
  void addIdPass(RenderGraph &graph, const SceneView &view, GraphImage ids, GraphImage depth);
  // Declares the selection mask pass: the items whose id is in `selected` drawn into `mask`
  // (IdFormat, cleared to 0) without a depth test, so the mask holds each selected entity's
  // whole silhouette, occluded or not. `selected` is copied. Adds nothing when it is empty.
  void addSelectionMaskPass(RenderGraph &graph, const SceneView &view, GraphImage mask,
                            std::span<const std::uint32_t> selected);
  // Declares the outline pass over `color`, drawing `outlineColor` next to the silhouettes in
  // `mask`. Adds nothing when the last mask pass had an empty selection.
  void addOutlinePass(RenderGraph &graph, GraphImage color, GraphImage mask,
                      glm::vec4 outlineColor = {1.0f, 0.6f, 0.1f, 1.0f});

  // Declares the debug line pass: the view's debug lines over `color`, depth-tested against
  // `depth` without writing it, so it has to come before a pass that discards the depth. Adds
  // nothing when there are no lines. Same lifetime rule as addScenePasses.
  void addDebugLinePass(RenderGraph &graph, const SceneView &view, GraphImage color, GraphImage depth);

  [[nodiscard]] const RenderStatistics &statistics() const noexcept {
    return m_statistics;
  }
  [[nodiscard]] const RendererSettings &settings() const noexcept {
    return m_settings;
  }
  // Whether cooked textures can be uploaded in the BC formats (docs/assets.md, "Textures").
  [[nodiscard]] bool blockCompressionSupported() const noexcept {
    return m_device.info().blockCompressionSupported;
  }
  void setSettings(const RendererSettings &settings) {
    m_settings = settings;
  }

  // The entry-point files the engine pipelines are built from, without the extension.
  [[nodiscard]] static std::span<const std::string_view> shaderNames() noexcept;
  // Rebuilds every pipeline built from the named module with new SPIR-V: the editor's hot
  // reload (docs/rendering.md, "Shaders"). A module or pipeline the driver rejects is reported
  // and leaves the old pipelines in place.
  [[nodiscard]] core::Result<void> reloadShader(std::string_view name, std::span<const std::byte> spirv);

private:
  struct Mesh {
    std::string debugName;
    rhi::BufferHandle vertices;
    rhi::BufferHandle indices;
    rhi::BufferHandle skin; // SkinWeights per vertex; invalid for a mesh that cannot be skinned
    std::uint32_t vertexCount{0};
    std::vector<Submesh> submeshes;
    Bounds bounds;
  };
  // The deformed vertices of one skinned instance, rewritten every frame it is drawn and
  // released a few frames after its last use.
  struct SkinnedVertices {
    MeshHandle mesh;
    rhi::BufferHandle buffer;
    std::uint64_t lastFrame{0};
  };
  // One instance to deform this frame.
  struct SkinJob {
    const Mesh *mesh;
    rhi::BufferHandle destination;
    std::uint32_t firstJoint;
    std::uint32_t jointCount;
  };
  struct Texture {
    std::string debugName;
    rhi::ImageHandle image;
  };
  struct Material {
    std::string debugName;
    MaterialDesc desc;
  };
  struct Environment {
    std::string debugName;
    rhi::ImageHandle equirectangular; // released once the cubes are computed
    rhi::ImageHandle skybox;
    rhi::ImageHandle irradiance;
    rhi::ImageHandle prefiltered;
    bool pending{true};
  };
  // The per-frame buffers every scene pass shares, allocated by the first pass that records.
  struct FrameBuffers {
    rhi::TransientAllocation frame;
    rhi::TransientAllocation objects;
    bool uploaded{false};
    [[nodiscard]] bool valid() const noexcept {
      return !frame.data.empty() && !objects.data.empty();
    }
  };
  // A draw resolved for recording: the order lists refer to these.
  struct ResolvedDraw {
    std::uint32_t objectIndex;
    const Mesh *mesh;
    std::uint64_t vertices; // the mesh's vertex address, or its skinned instance's
    Submesh submesh;
    bool doubleSided;
    bool blended;
    bool masked;
    float viewDepth;
  };
  struct Cascade {
    glm::mat4 matrix{1.0f};
    float split{0.0f};
  };
  // The persistent images the forward pass reads, imported into the frame's graph.
  struct FrameImages {
    GraphImage lut;
    GraphImage skybox;
    GraphImage irradiance;
    GraphImage prefiltered;
  };

  // Every pipeline is recorded with the module it comes from, so a reload rebuilds it.
  struct PipelineSlot {
    std::string shader;
    std::variant<rhi::GraphicsPipelineDesc, rhi::ComputePipelineDesc> desc;
    rhi::PipelineHandle *target;
  };

  void defineGraphics(rhi::PipelineHandle &target, std::string shader, rhi::GraphicsPipelineDesc desc);
  void defineCompute(rhi::PipelineHandle &target, std::string shader, const char *entry, const char *name);
  [[nodiscard]] rhi::PipelineHandle createPipeline(const PipelineSlot &slot, rhi::ShaderHandle shader);
  void createPipelines(const std::filesystem::path &shaderDir);
  void createDefaults();
  [[nodiscard]] std::uint32_t materialIndex(MaterialHandle handle) const;
  [[nodiscard]] std::uint32_t sampledIndex(rhi::ImageHandle image) const;

  // Sorts and resolves the view's draws for the frame and declares the skinning pass; a graph
  // frame that already prepared this view keeps its state.
  void prepareFrame(RenderGraph &graph, const SceneView &view, glm::uvec2 targetSize);
  // The address the draw pulls its vertices from: its skinned instance's buffer, created or
  // reused here, when it is a valid skinned draw, the mesh's otherwise.
  [[nodiscard]] std::uint64_t resolveVertices(const DrawItem &item, const Mesh &mesh, const SceneView &view);
  void recordSkinning(rhi::ICommandList &commands);
  void releaseSkinnedVertices(bool all);
  void computeCascades(const SceneView &view, float aspect);
  // Allocates and fills the frame constants, objects, materials and lights once per frame.
  void ensureFrameUploaded(const PassResources &resources);
  void bindFrame(rhi::ICommandList &commands);
  void recordDraws(rhi::ICommandList &commands, std::span<const std::uint32_t> order,
                   std::span<const rhi::PipelineHandle, 2> pipelines, bool count, std::uint32_t cascade = 0,
                   std::span<const std::uint32_t> only = {});
  void recordClustering(rhi::ICommandList &commands);
  void recordPost(rhi::ICommandList &commands, rhi::PipelineHandle pipeline, rhi::ImageHandle source,
                  rhi::ImageHandle secondary, glm::uvec2 targetSize);
  void addPrecomputePasses(RenderGraph &graph, const SceneView &view);
  void addEnvironmentPasses(RenderGraph &graph, EnvironmentHandle handle, Environment &environment, bool viewed);
  void addBloomPasses(RenderGraph &graph, GraphImage hdr, glm::uvec2 size, GraphImage &result);
  void recordOutline(rhi::ICommandList &commands, rhi::ImageHandle mask);
  void recordDebugLines(rhi::ICommandList &commands, const SceneView &view, glm::uvec2 targetSize);

  rhi::IDevice &m_device;
  RendererSettings m_settings;
  std::vector<PipelineSlot> m_pipelineSlots;

  std::array<rhi::PipelineHandle, 2> m_depthPipelines;   // by doubleSided
  std::array<rhi::PipelineHandle, 2> m_shadowPipelines;  // both cull nothing; kept as a pair for recordDraws
  std::array<rhi::PipelineHandle, 2> m_forwardPipelines; // by doubleSided
  std::array<rhi::PipelineHandle, 2> m_blendPipelines;   // by doubleSided
  std::array<rhi::PipelineHandle, 2> m_idPipelines;
  std::array<rhi::PipelineHandle, 2> m_maskPipelines;
  rhi::PipelineHandle m_skyboxPipeline;
  rhi::PipelineHandle m_bloomDownPipeline;
  rhi::PipelineHandle m_bloomUpPipeline;
  rhi::PipelineHandle m_tonemapPipeline;
  rhi::PipelineHandle m_fxaaPipeline;
  rhi::PipelineHandle m_outlinePipeline;
  rhi::PipelineHandle m_debugLinePipeline;
  rhi::PipelineHandle m_clusterPipeline;
  rhi::PipelineHandle m_equirectPipeline;
  rhi::PipelineHandle m_cubeMipPipeline;
  rhi::PipelineHandle m_irradiancePipeline;
  rhi::PipelineHandle m_prefilterPipeline;
  rhi::PipelineHandle m_brdfLutPipeline;
  rhi::PipelineHandle m_skinPipeline;

  std::array<rhi::SamplerHandle, 3> m_materialSamplers; // by TextureWrap
  rhi::SamplerHandle m_linearClampSampler;
  rhi::SamplerHandle m_shadowSampler;
  TextureHandle m_whiteTexture;
  TextureHandle m_flatNormalTexture;
  rhi::ImageHandle m_brdfLut;
  bool m_brdfLutPending{true};
  rhi::BufferHandle m_clusterBuffer;

  core::HandlePool<Mesh, MeshTag> m_meshes;
  core::HandlePool<Texture, TextureTag> m_textures;
  core::HandlePool<Material, MaterialTag> m_materials;
  core::HandlePool<Environment, EnvironmentTag> m_environments;
  std::unordered_map<std::uint64_t, SkinnedVertices> m_skinned; // by DrawItem::skinInstance
  std::uint32_t m_materialSlots{0};                             // highest material index plus one, the GPU array's size
  RenderStatistics m_statistics;

  // Frame state between addScenePasses and the graph's execute.
  const SceneView *m_view{nullptr};
  const RenderGraph *m_graph{nullptr};
  std::uint64_t m_graphFrame{0};
  glm::uvec2 m_targetSize{0, 0};
  std::vector<ResolvedDraw> m_resolved;
  std::vector<SkinJob> m_skinJobs;
  std::vector<std::uint32_t> m_opaqueOrder;  // opaque and masked, grouped by pipeline and mesh
  std::vector<std::uint32_t> m_blendedOrder; // back to front
  std::vector<std::uint32_t> m_allOrder;     // for the id and mask passes
  std::array<Cascade, CascadeCount> m_cascades;
  std::array<GraphImage, CascadeCount> m_cascadeImages;
  FrameImages m_frameImages;
  bool m_cascadesActive{false};
  FrameBuffers m_frameBuffers;
  std::vector<std::uint32_t> m_selected; // sorted and unique, for the binary search per draw
  glm::vec4 m_outlineColor{1.0f, 0.6f, 0.1f, 1.0f};
};

} // namespace sonnet::renderer
