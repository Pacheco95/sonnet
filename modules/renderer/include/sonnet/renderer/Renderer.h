#pragma once

#include <sonnet/renderer/Material.h>
#include <sonnet/renderer/Mesh.h>
#include <sonnet/renderer/RenderGraph.h>
#include <sonnet/renderer/SceneView.h>
#include <sonnet/renderer/Texture.h>

#include <sonnet/core/Error.h>
#include <sonnet/core/HandlePool.h>
#include <sonnet/core/JobSystem.h>
#include <sonnet/rhi/Device.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace sonnet::renderer {

struct RenderStatistics {
  // What was submitted, not what survived culling: the GPU decides that and the CPU never reads
  // the counts back (ADR-0012).
  std::uint32_t drawCount{0}; // scene draws: opaque and blended, not the shadow, id or mask passes
  std::uint32_t triangleCount{0};
  std::uint32_t shadowDrawCount{0};     // the sun's cascades and the local shadow maps
  std::uint32_t localShadowCount{0};    // spot and point lights whose shadow maps this view rendered
  std::uint32_t localShadowMapCount{0}; // the depth maps they took: one per spot light, six per point light
  std::uint32_t indirectCallCount{0};   // indirect draw calls the scene passes recorded, one per batch
  std::uint32_t lightCount{0};
  std::uint32_t skinnedInstanceCount{0}; // instances the skinning pass deformed
  std::uint32_t skinnedVertexCount{0};
  std::uint32_t particleEmitterCount{0}; // emitters the view drew
  std::uint32_t particleSlotCount{0};    // their particle capacity: the ring slots simulated, alive or not
};

// One term of the forward shading written in place of the final colour, before tone mapping, to
// compare a platform's lighting term by term against another's (docs/rendering.md, "Debugging").
enum class DebugView : std::uint32_t {
  Final,
  Albedo,
  Normal,       // the shading normal, mapped from [-1, 1] to [0, 1]
  SunDirect,    // the sun's contribution without its shadow
  ShadowFactor, // the sun's visibility, white where lit
  IblDiffuse,
  IblSpecular,
  BrdfLut, // the split-sum scale and bias at the fragment's n.v and roughness, in red and green
  Cascade, // the view-depth cascade, red, green, blue, then yellow
};
constexpr std::uint32_t DebugViewCount = 9;
// What the editor's View > Shading term menu calls a term: "Final", "Sun direct", "BRDF LUT".
[[nodiscard]] const char *debugViewName(DebugView view) noexcept;

// Quality knobs. Tests turn the sizes and sample counts down so Lavapipe finishes quickly.
struct RendererSettings {
  bool shadows{true};
  std::uint32_t shadowMapSize{2048}; // per cascade
  float shadowDistance{80.0f};       // metres of view depth the cascades cover
  float shadowBias{0.0015f}; // the cascades', in reversed-Z depth units; receiver offset also follows the texel size
  // Spot and point lights that ask for a shadow (Light::castsShadows) get depth maps, per view, up
  // to this many lights; the rest still illuminate, without shadows. A spot light takes one map and
  // a point light six, one per cube face, and a view has Renderer::MaxLocalShadows maps in all, so
  // point lights can reach the map limit before they reach this count.
  std::uint32_t maxLocalShadows{8};
  std::uint32_t localShadowMapSize{1024}; // per spot light, and per face of a point light
  DebugView debugView{DebugView::Final};
  bool bloom{true};
  std::uint32_t bloomLevels{5};
  bool antialiasing{true};
  // The format `addPresentPass` writes into, typically a swapchain's. Undefined leaves the
  // present pipeline out, which is what the editor does: it shows the scene through Dear ImGui.
  rhi::Format presentFormat{rhi::Format::Undefined};
  std::uint32_t environmentSize{512}; // the skybox cube, with a full mip chain
  std::uint32_t irradianceSize{32};
  std::uint32_t irradianceSamples{256};
  std::uint32_t prefilteredSize{128};
  std::uint32_t prefilteredLevels{5};
  std::uint32_t prefilterSamples{256};
  std::uint32_t brdfLutSize{256};
  std::uint32_t brdfLutSamples{256};
  // The pool the per-frame object and cull-candidate fill is spread over
  // ([ADR-0013](docs/decisions/0013-job-system.md)). Null runs it on the calling thread, which is what the tests and
  // the cook tool get; the result is the same either way, since every iteration writes one slot of its own.
  core::JobSystem *jobs{nullptr};
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
  // Views a frame can declare with shadows (the editor's Scene and Game views). Each takes its
  // cascades and its local shadow maps from the device's 64 bindless depth images (ADR-0017), so
  // a view's local budget is what is left of its half.
  static constexpr std::uint32_t MaxShadowedViews = 2;
  static constexpr std::uint32_t MaxLocalShadows = rhi::MaxBindlessDepthImages / MaxShadowedViews - CascadeCount;
  // The faces of a point light's shadow cube, in the order of their maps: +X, -X, +Y, -Y, +Z, -Z.
  static constexpr std::uint32_t PointShadowFaces = 6;
  // Culling jobs one frame can reserve over each order list: the four cascades, the depth
  // pre-pass and the forward pass over the opaque draws, plus one per local shadow map the
  // lights ask for within the budget, and the id and selection-mask passes over all of them. The command buffer and
  // the visible list are sized for exactly these (ADR-0016). CullJobsOpaque is the part the
  // settings do not change.
  static constexpr std::uint32_t CullJobsOpaque = CascadeCount + 2;
  static constexpr std::uint32_t CullJobsAll = 2;

  // shaderDir holds the modules compiled by sonnet_add_engine_shaders (`forward.spv`, ...). It is
  // read through platform::Platform::openContent: a relative directory is in the content root
  // (the APK's assets/ on Android, beside the binary elsewhere), an absolute one is on disk.
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
  // Forgets an emitter's particles, so it starts over, with its burst, the next time it is drawn.
  void resetParticles(std::uint64_t key);

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
  // structure"). Several views can be declared into one graph, each into its own target: the
  // renderer keeps what is per view (the orders, the batches, the cull jobs, the cascades, the
  // frame constants) apart and shares the objects, materials, lights' storage, skinned vertices
  // and cull buffers, which are sliced per view (docs/decisions/0021-two-views-in-one-frame.md).
  // The passes: the shadow cascades, the depth pre-pass, light clustering, the forward pass into
  // an HDR image, the skybox, the blended draws, bloom, tone mapping into `color` and FXAA, plus
  // any pending environment or lookup-table precomputation. `view` and the spans it holds must
  // outlive the graph's execute. The first pass the renderer declares in a graph frame, this or
  // the id or mask pass, is preceded by the skinning pass when the view has skinned draws.
  void addScenePasses(RenderGraph &graph, const SceneView &view, GraphImage color, GraphImage depth,
                      glm::vec4 clearColor = {0.05f, 0.05f, 0.07f, 1.0f});
  // Declares the present pass: `source`, already tone-mapped and display-encoded by the scene
  // passes, copied into `target`, which may have another format. The player puts its scene into
  // the acquired swapchain image this way (docs/rendering.md, "Frame structure"). Adds nothing
  // when the settings named no present format.
  void addPresentPass(RenderGraph &graph, GraphImage source, GraphImage target);
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

  // What the scene passes of the frame's `view`th declared view submitted; the first is the one
  // a single-view frame has.
  [[nodiscard]] const RenderStatistics &statistics(std::size_t view = 0) const noexcept {
    return view < m_viewCount ? m_views[view]->statistics : m_noStatistics;
  }
  [[nodiscard]] const RendererSettings &settings() const noexcept {
    return m_settings;
  }
  // The local shadow budget the settings give, in lights: maxLocalShadows, within what the depth
  // array holds.
  [[nodiscard]] std::uint32_t localShadowBudget() const noexcept {
    return m_settings.shadows ? std::min(m_settings.maxLocalShadows, MaxLocalShadows) : 0u;
  }
  // One local shadow map as the frame's `view`th declared view rendered it: which entry of
  // SceneView::lights it belongs to, its light-space matrix and the graph image holding the depth.
  // A spot light has one; a point light has PointShadowFaces in a row, `face` counting them.
  // Valid for the frame, from addScenePasses to the next graph reset.
  struct LocalShadow {
    std::uint32_t light{0};
    std::uint32_t face{0};
    glm::mat4 matrix{1.0f};
    GraphImage image;
  };
  [[nodiscard]] std::span<const LocalShadow> localShadows(std::size_t view = 0) const noexcept {
    return view < m_viewCount ? std::span<const LocalShadow>{m_views[view]->localShadows}
                              : std::span<const LocalShadow>{};
  }
  // The device the renderer draws with: which compressed formats cooked textures can be loaded
  // in (docs/assets.md, "Textures") and which bundles it can run (docs/player.md).
  [[nodiscard]] const rhi::DeviceInfo &deviceInfo() const noexcept {
    return m_device.info();
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
    rhi::BufferHandle skin;  // SkinWeights per vertex; invalid for a mesh that cannot be skinned
    rhi::BufferHandle morph; // MorphDeltas, target after target; invalid for a mesh without targets
    std::uint32_t morphTargetCount{0};
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
  // One emitter's particles between frames (ADR-0023): a ring of slots, the alive list the
  // simulation rebuilds every frame, and the indirect command whose instance count it counts.
  struct ParticleState {
    rhi::BufferHandle particles;
    rhi::BufferHandle alive;
    rhi::BufferHandle command;
    std::uint32_t maxParticles{0};
    std::uint32_t head{0};   // the ring slot the next particle starts in
    std::uint32_t serial{0}; // particles started so far, which seeds them
    float carry{0.0f};       // the fraction of a particle the rate owes
    bool burstDone{false};
    std::uint64_t lastFrame{0};
    std::uint64_t simulatedFrame{~std::uint64_t{0}};
    std::uint64_t params{0}; // the frame's parameters, set by the pass that simulates
  };
  // One emitter to draw this frame, and to simulate when this view is the first to see it.
  struct ParticleJob {
    ParticleState *state;
    const ParticleEmitterItem *item;
    bool simulate;
    std::uint32_t spawnCount;
    std::uint32_t spawnHead;
    std::uint32_t serial;
    float dt;
    float distance;
  };
  // One instance to deform this frame.
  struct SkinJob {
    const Mesh *mesh;
    rhi::BufferHandle destination;
    std::uint32_t firstJoint;
    std::uint32_t jointCount;
    std::uint32_t firstMorphWeight;
    std::uint32_t morphCount;
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
    std::uint64_t opaqueCandidates{0}; // addresses of the culling pass's input arrays
    std::uint64_t allCandidates{0};
    bool uploaded{false};
    bool directSlotsUploaded{false}; // the visible list's direct range, written on the first direct draw
    [[nodiscard]] bool valid() const noexcept {
      return !frame.data.empty() && !objects.data.empty();
    }
  };
  // A draw resolved for recording: the order lists refer to these.
  struct ResolvedDraw {
    std::uint32_t objectIndex;
    const Mesh *mesh;
    std::uint32_t vertexBuffer; // bindless index into vertexBuffers[] (ADR-0015)
    Submesh submesh;
    glm::vec3 center; // world-space bounds, what the culling pass tests
    glm::vec3 extent; // half size
    bool doubleSided;
    bool mirrored; // the transform's upper 3x3 has a negative determinant: the winding reverses
    bool blended;
    bool masked;
    float viewDepth;
  };
  // A run of draws in one order list sharing a pipeline, a front face, a mesh and a submesh,
  // submitted as the instances of one indirect command against that mesh's index buffer
  // (ADR-0016).
  struct Batch {
    const Mesh *mesh;
    Submesh submesh;
    std::uint32_t pipeline;  // index into a pipeline pair: 1 for double-sided
    bool mirrored;           // drawn with a clockwise front face
    std::uint32_t firstDraw; // into the order list, and into a job's run of the visible list
    std::uint32_t drawCount;
  };
  // One culling dispatch: a frustum over one order list, writing one pass's commands, one per
  // batch, and the visible list their instances read.
  struct CullJob {
    glm::mat4 viewProjection{1.0f};
    bool opaque{true}; // which order list's candidate array it tests
    std::uint32_t drawCount{0};
    std::uint32_t batchCount{0};
    std::uint32_t firstCommand{0}; // the job's first batch's command in the command buffer
    std::uint32_t firstVisible{0}; // the job's first slot in the visible list
    bool selectedOnly{false};
  };
  struct Cascade {
    glm::mat4 matrix{1.0f};
    float split{0.0f};
    float blendStart{0.0f};
    float texelSize{0.0f};
  };
  // The persistent images the forward pass reads, imported into the frame's graph.
  struct FrameImages {
    GraphImage lut;
    GraphImage skybox;
    GraphImage irradiance;
    GraphImage prefiltered;
  };

  // Everything one view keeps between addScenePasses and the graph's execute. The passes capture
  // a pointer to their view's state, so a second view's declaration does not touch the first's.
  struct ViewState {
    const SceneView *view{nullptr};
    glm::uvec2 targetSize{0, 0};
    RenderStatistics statistics;
    std::vector<ResolvedDraw> resolved;
    std::vector<SkinJob> skinJobs;
    std::vector<ParticleJob> particleJobs;
    std::vector<std::uint32_t> opaqueOrder;  // opaque and masked, grouped into batches
    std::vector<std::uint32_t> blendedOrder; // back to front
    std::vector<std::uint32_t> allOrder;     // for the id and mask passes, grouped the same way
    std::vector<Batch> opaqueBatches;
    std::vector<Batch> allBatches;
    std::vector<CullJob> cullJobs;   // reserved this frame, run by the culling passes
    std::uint32_t opaqueJobsUsed{0}; // of opaqueJobCapacity
    std::uint32_t opaqueJobCapacity{CullJobsOpaque};
    std::uint32_t allJobsUsed{0};   // of CullJobsAll
    std::size_t firstPendingJob{0}; // jobs a culling pass has not recorded yet
    std::array<Cascade, CascadeCount> cascades;
    std::array<GraphImage, CascadeCount> cascadeImages;
    bool cascadesActive{false};
    std::vector<LocalShadow> localShadows; // the budgeted lights' maps, in the light list's order
    FrameBuffers frameBuffers;
    // This view's slices of the shared command and visible buffers, in commands and slots.
    std::uint32_t commandBase{0};
    std::uint32_t visibleBase{0};
    std::uint32_t directBase{0};            // the direct range's first slot this frame
    std::vector<std::uint32_t> directSlots; // its contents, the object index of each resolved draw
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
  // Runs `body` over [0, count) across the settings' pool, or on this thread when there is none.
  void parallelFor(const char *name, std::size_t count, std::size_t grain,
                   const std::function<void(std::size_t, std::size_t)> &body) const;

  [[nodiscard]] ViewState &prepareFrame(RenderGraph &graph, const SceneView &view, glm::uvec2 targetSize);
  // The bindless index of the buffer the draw pulls its vertices from: its skinned instance's
  // buffer, created or reused here, when it is a valid skinned draw, the mesh's otherwise.
  // InvalidBindlessIndex when the array is full.
  [[nodiscard]] std::uint32_t resolveVertices(ViewState &v, const DrawItem &item, const Mesh &mesh);
  void recordSkinning(rhi::ICommandList &commands, const ViewState &v);
  // Resolves the view's emitters into jobs, sorted far to near, and declares the pass that
  // simulates the ones not yet advanced this frame.
  void addParticlePass(RenderGraph &graph, ViewState &v);
  void recordParticleSimulation(rhi::ICommandList &commands, ViewState &v);
  void recordParticleDraws(rhi::ICommandList &commands, const ViewState &v);
  void releaseParticles(bool all);
  void releaseSkinnedVertices(bool all);
  void computeCascades(ViewState &v, float aspect);
  void selectLocalShadows(ViewState &v) const;
  // How many local shadow maps the view's lights get, which ensureIndirectBuffers sizes the cull
  // buffers for before selectLocalShadows picks them.
  [[nodiscard]] std::uint32_t localShadowMapCount(const SceneView &view) const;
  // Groups an order list, already sorted by pipeline, front face, mesh and submesh, into the runs
  // one instanced indirect command each can submit. Returns the batches; the order list's entries
  // keep their positions.
  void buildBatches(const ViewState &v, std::span<const std::uint32_t> order, std::vector<Batch> &batches) const;
  // Grows the command buffer and the visible list to what this frame's batches and draws need.
  void ensureIndirectBuffers(ViewState &v);
  // Allocates and fills the frame constants, objects, materials, lights and cull candidates
  // once per frame.
  void ensureFrameUploaded(ViewState &v, const PassResources &resources);
  void bindFrame(rhi::ICommandList &commands, const ViewState &v);
  // Reserves a job's command and visible-list ranges, or nothing when the frame has no room left.
  [[nodiscard]] std::optional<CullJob> reserveCullJob(ViewState &v, bool opaque);
  // Declares the culling pass the editor's id or selection-mask pass needs, over all draws.
  [[nodiscard]] std::optional<CullJob> addCullPass(RenderGraph &graph, ViewState &v, bool selectedOnly);
  // Empties every reserved job's commands, then runs each job's frustum test, with the barriers
  // that order the previous frame's reads and this frame's command fetch around them.
  void recordCulling(rhi::ICommandList &commands, ViewState &v);
  // One indirect command per batch, the job's own, whose instances are the batch's survivors
  // (ADR-0016).
  void recordIndirect(rhi::ICommandList &commands, ViewState &v, const CullJob &job, std::span<const Batch> batches,
                      std::span<const rhi::PipelineHandle, 2> pipelines, std::uint32_t shadow = 0);
  // The direct path, which the blended draws keep because their order is view-dependent. Each
  // draw names its slot in the visible list's direct range as its first instance.
  void recordDraws(rhi::ICommandList &commands, ViewState &v, std::span<const std::uint32_t> order,
                   std::span<const rhi::PipelineHandle, 2> pipelines, bool count, std::uint32_t shadow = 0);
  void recordClustering(rhi::ICommandList &commands, const ViewState &v);
  void recordPost(rhi::ICommandList &commands, const SceneView *view, rhi::PipelineHandle pipeline,
                  rhi::ImageHandle source, rhi::ImageHandle secondary, glm::uvec2 targetSize);
  void addPrecomputePasses(RenderGraph &graph, const SceneView &view);
  // The graph's imports of an environment's cubes, made once per graph frame however many views
  // sample them, so the passes that compute them and every view's reads share one image each.
  struct EnvironmentImport {
    EnvironmentHandle handle;
    GraphImage skybox;
    GraphImage irradiance;
    GraphImage prefiltered;
  };
  [[nodiscard]] EnvironmentImport importEnvironment(RenderGraph &graph, EnvironmentHandle handle,
                                                    const Environment &environment);
  void addEnvironmentPasses(RenderGraph &graph, EnvironmentHandle handle, Environment &environment);
  void addBloomPasses(RenderGraph &graph, const SceneView &view, GraphImage hdr, glm::uvec2 size, GraphImage &result);
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
  rhi::PipelineHandle m_presentPipeline; // only when the settings named a present format
  rhi::PipelineHandle m_outlinePipeline;
  rhi::PipelineHandle m_debugLinePipeline;
  rhi::PipelineHandle m_clusterPipeline;
  rhi::PipelineHandle m_cullPipeline;
  rhi::PipelineHandle m_clearCommandsPipeline;
  rhi::PipelineHandle m_equirectPipeline;
  rhi::PipelineHandle m_cubeMipPipeline;
  rhi::PipelineHandle m_irradiancePipeline;
  rhi::PipelineHandle m_prefilterPipeline;
  rhi::PipelineHandle m_brdfLutPipeline;
  rhi::PipelineHandle m_skinPipeline;
  rhi::PipelineHandle m_particleResetPipeline;
  rhi::PipelineHandle m_particleSimulatePipeline;
  std::array<rhi::PipelineHandle, 2> m_particleDrawPipelines; // by ParticleBlend
  rhi::BufferHandle m_particleQuad;                           // the six indices of a quad's two triangles

  std::array<rhi::SamplerHandle, 3> m_materialSamplers; // by TextureWrap
  rhi::SamplerHandle m_linearClampSampler;
  rhi::SamplerHandle m_shadowSampler;
  TextureHandle m_whiteTexture;
  TextureHandle m_flatNormalTexture;
  rhi::ImageHandle m_brdfLut;
  bool m_brdfLutPending{true};
  rhi::BufferHandle m_clusterBuffer;
  // The draw commands the culling pass writes, one per batch per job, and the visible list of
  // object indices their instances read: a run per job, then the direct range with one slot per
  // resolved draw for the direct path. Device local and rewritten every frame (ADR-0016).
  rhi::BufferHandle m_commandBuffer;
  rhi::BufferHandle m_visibleBuffer;
  std::uint32_t m_commandCapacity{0};
  std::uint32_t m_visibleCapacity{0};

  core::HandlePool<Mesh, MeshTag> m_meshes;
  core::HandlePool<Texture, TextureTag> m_textures;
  core::HandlePool<Material, MaterialTag> m_materials;
  core::HandlePool<Environment, EnvironmentTag> m_environments;
  std::unordered_map<std::uint64_t, SkinnedVertices> m_skinned; // by DrawItem::skinInstance
  std::unordered_map<std::uint64_t, ParticleState> m_particles; // by ParticleEmitterItem::key
  std::uint32_t m_materialSlots{0};                             // highest material index plus one, the GPU array's size
  RenderStatistics m_noStatistics;

  // The views declared into the graph frame `m_graphSerial`; the states past m_viewCount are kept
  // for their vectors' capacity.
  std::vector<std::unique_ptr<ViewState>> m_views;
  std::size_t m_viewCount{0};
  std::uint64_t m_graphSerial{0};      // RenderGraph::frameSerial of the frame prepared
  std::uint64_t m_graphFrame{0};       // its frameIndex, which the skinned buffers age by
  std::uint32_t m_commandsReserved{0}; // slices of the two shared buffers the views so far have taken
  std::uint32_t m_visibleReserved{0};
  FrameImages m_frameImages;       // the view being declared's
  std::uint64_t m_importSerial{0}; // the graph frame the imports below belong to
  GraphImage m_lutImport;
  std::vector<EnvironmentImport> m_environmentImports;
  std::vector<std::uint32_t> m_selected; // sorted and unique, for the binary search per draw
  glm::vec4 m_outlineColor{1.0f, 0.6f, 0.1f, 1.0f};
};

} // namespace sonnet::renderer
