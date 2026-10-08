#include <sonnet/renderer/Renderer.h>

#include <sonnet/core/Assert.h>
#include <sonnet/core/Error.h>
#include <sonnet/core/Log.h>
#include <sonnet/core/Profile.h>
#include <sonnet/platform/Platform.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <format>
#include <type_traits>
#include <utility>

namespace sonnet::renderer {

const char *debugViewName(DebugView view) noexcept {
  static constexpr std::array<const char *, DebugViewCount> Names{
      "Final", "Albedo", "Normal", "Sun direct", "Shadow factor", "IBL diffuse", "IBL specular", "BRDF LUT", "Cascade"};
  const auto index = static_cast<std::size_t>(view);
  return index < Names.size() ? Names[index] : "";
}

namespace {

// Mirrors of shaders/sonnet.slang under scalar block layout.

// How much of a per-frame fill is worth handing to another worker. Small enough that a modest
// scene still spreads, large enough that a handful of draws does not pay for the scheduling.
constexpr std::size_t ObjectGrain = 2048;
constexpr std::size_t BatchGrain = 16;

constexpr std::uint32_t LightPoint = 0;
constexpr std::uint32_t LightSpot = 1;
constexpr std::uint32_t ClusterGridX = 16;
constexpr std::uint32_t ClusterGridY = 9;
constexpr std::uint32_t ClusterGridZ = 24;
constexpr std::uint32_t ClusterCount = ClusterGridX * ClusterGridY * ClusterGridZ;
constexpr std::uint32_t ClusterBytes = 64 * sizeof(std::uint32_t);
// View depth beyond which lights are not clustered.
constexpr float ClusterFar = 500.0f;
constexpr std::uint32_t AlphaOpaque = 0;
constexpr std::uint32_t AlphaMask = 1;
constexpr std::uint32_t AlphaBlend = 2;
constexpr std::uint32_t MaterialDoubleSided = 1u << 8;

struct GpuLight {
  glm::vec3 position;
  float range;
  glm::vec3 color;
  std::uint32_t type;
  glm::vec3 direction;
  float innerCos;
  float outerCos;
  std::uint32_t shadow; // index into FrameConstants::localShadows, NoLocalShadow without one
  float padding[2];
};
static_assert(sizeof(GpuLight) == 64);

constexpr std::uint32_t NoLocalShadow = 0xFFFFFFFFu;

// One local shadow map, a spot light's or one face of a point light's: its light-space matrix
// and the bindless depth slot it was drawn into. Mirror of LocalShadow in shaders/sonnet.slang.
struct GpuLocalShadow {
  glm::mat4 matrix;
  std::uint32_t image;
  std::uint32_t padding[3];
};
static_assert(sizeof(GpuLocalShadow) == 80);

struct GpuMaterial {
  glm::vec4 baseColor;
  glm::vec4 emissive;
  float metallic;
  float roughness;
  float normalScale;
  float occlusionStrength;
  std::uint32_t baseColorTexture;
  std::uint32_t metallicRoughnessTexture;
  std::uint32_t normalTexture;
  std::uint32_t occlusionTexture;
  std::uint32_t emissiveTexture;
  std::uint32_t flags;
  std::uint32_t sampler;
  std::uint32_t padding;
};
static_assert(sizeof(GpuMaterial) == 80);

struct FrameConstants {
  glm::mat4 view;
  glm::mat4 projection;
  glm::mat4 viewProjection;
  glm::mat4 inverseProjection;
  glm::mat4 inverseViewProjection;
  glm::mat4 cascadeMatrices[Renderer::CascadeCount];
  glm::vec4 cascadeSplits;
  glm::vec4 cascadeBlendStarts;
  glm::vec4 cascadeTexelSizes;
  glm::vec4 cameraPosition;
  glm::vec4 sunDirection;
  glm::vec4 sunColor;
  glm::vec4 ambient;
  glm::vec4 environment;
  glm::vec4 clusterScaleBias;
  glm::vec2 targetSize;
  float nearPlane;
  float shadowBias;
  std::uint32_t cascadeImages[Renderer::CascadeCount];
  std::uint32_t irradianceCube;
  std::uint32_t prefilteredCube;
  std::uint32_t skyboxCube;
  std::uint32_t brdfLut;
  std::uint32_t linearSampler;
  std::uint32_t shadowSampler;
  std::uint32_t lightCount;
  std::uint32_t debugView;
  std::uint64_t visible; // the object index of each instance an indirect or direct draw submits (ADR-0016)
  std::uint64_t materials;
  std::uint64_t lights;
  std::uint64_t clusters;
  std::uint64_t localShadows; // GpuLocalShadow per local shadow map, 0 without any
};
static_assert(sizeof(FrameConstants) == 824);

// No normal matrix: the vertex shader derives it from model (sonnet.slang, transformNormal).
struct ObjectData {
  glm::mat4 model;
  glm::vec4 color;
  std::uint32_t id;
  std::uint32_t material;
  std::uint32_t vertexBuffer; // bindless index into vertexBuffers[] (ADR-0015)
  std::uint32_t vertexPad{0};
};
static_assert(sizeof(ObjectData) == 96);

struct DrawConstants {
  std::uint32_t shadow; // a cascade, or CascadeCount plus a local shadow map's index; unused elsewhere
};
static_assert(sizeof(DrawConstants) == 4);

// Mirror of CullDraw in shaders/sonnet.slang and shaders/cull.slang.
struct CullDraw {
  glm::vec3 center;
  glm::vec3 extent;
  std::uint32_t objectIndex;
  std::uint32_t indexCount;
  std::uint32_t firstIndex;
  std::uint32_t batch;
  std::uint32_t batchFirst;
  std::uint32_t flags;
};
static_assert(sizeof(CullDraw) == 48);
constexpr std::uint32_t CullSelected = 1u << 0;

// Mirror of shaders/cull.slang.
struct CullConstants {
  glm::mat4 viewProjection;
  std::uint64_t draws;
  std::uint64_t visible;
  std::uint32_t drawCount;    // candidates to test, or commands to empty
  std::uint32_t commandBase;  // the job's first command
  std::uint32_t visibleBase;  // the job's first slot in the visible list
  std::uint32_t selectedOnly; // the selection mask pass keeps only the selected draws
};
static_assert(sizeof(CullConstants) == 96);
static_assert(sizeof(CullConstants) <= rhi::PushConstantSize);
constexpr std::uint32_t CullThreads = 64;

// Mirror of shaders/ibl.slang.
struct IblConstants {
  std::uint32_t source;
  std::uint32_t destination;
  std::uint32_t size;
  std::uint32_t sampleCount;
  float roughness;
  float sourceLod;
  std::uint32_t sampler;
  std::uint32_t padding{0};
};
static_assert(sizeof(IblConstants) == 32);

// Mirror of shaders/post.slang.
struct PostConstants {
  std::uint32_t source;
  std::uint32_t secondary;
  std::uint32_t sampler;
  std::uint32_t padding{0};
  glm::vec2 sourceTexel;
  glm::vec2 targetSize;
  float exposure;
  float bloomStrength;
  float padding1{0.0f};
  float padding2{0.0f};
};
static_assert(sizeof(PostConstants) == 48);

// Mirror of shaders/outline.slang.
struct OutlineConstants {
  glm::vec4 color;
};
static_assert(sizeof(OutlineConstants) == 16);

// Matches debug.slang.
struct DebugVertex {
  glm::vec4 position;
  glm::vec4 color;
};
static_assert(sizeof(DebugVertex) == 32);

struct DebugConstants {
  glm::mat4 viewProjection;
};
static_assert(sizeof(DebugConstants) <= rhi::PushConstantSize);

// Mirror of shaders/skin.slang.
struct SkinConstants {
  std::uint64_t source;
  std::uint64_t skin;
  std::uint64_t joints;
  std::uint64_t destination;
  std::uint64_t morph;
  std::uint64_t weights;
  std::uint32_t vertexCount;
  std::uint32_t jointCount;
  std::uint32_t morphCount;
  std::uint32_t padding;
};
static_assert(sizeof(SkinConstants) == 64);
constexpr std::uint32_t SkinThreads = 64;

// Mirror of shaders/particles.slang.
struct ParticleSimulateConstants {
  std::uint64_t particles;
  std::uint64_t alive;
  std::uint64_t params;
};
struct ParticleDrawConstants {
  glm::mat4 viewProjection;
  glm::vec3 cameraRight;
  glm::vec3 cameraUp;
  std::uint64_t particles;
  std::uint64_t alive;
  std::uint64_t params;
};
static_assert(sizeof(ParticleDrawConstants) == 112);
static_assert(sizeof(ParticleDrawConstants) <= rhi::PushConstantSize);
struct ParticleParams {
  glm::mat4 model;
  glm::vec4 colorStart;
  glm::vec4 colorEnd;
  glm::vec3 gravity;
  float drag;
  glm::vec2 lifetime;
  glm::vec2 speed;
  float coneAngle;
  float sizeStart;
  float sizeEnd;
  float dt;
  std::uint32_t maxParticles;
  std::uint32_t spawnCount;
  std::uint32_t spawnHead;
  std::uint32_t serial;
  std::uint32_t seed;
  std::uint32_t texture;
  std::uint32_t flags;
  std::uint32_t sampler;
};
static_assert(sizeof(ParticleParams) == 176);
constexpr std::uint32_t ParticleFlagLocal = 1u << 0;
constexpr std::uint32_t ParticleFlagAdditive = 1u << 1;
constexpr std::uint32_t ParticleThreads = 64;
constexpr std::uint32_t MaxParticlesPerEmitter = 1u << 20;
// The most a frame advances a particle by, so a hitch does not fire a second's worth at once.
constexpr float MaxParticleStep = 0.25f;
// Graph frames an emitter's particles outlive its last draw.
constexpr std::uint64_t ParticleStateFrames = 8;
// Graph frames a skinned instance's buffer outlives its last draw, so an instance hidden for a
// moment does not reallocate.
constexpr std::uint64_t SkinnedBufferFrames = 8;

std::uint32_t groups(std::uint32_t size, std::uint32_t threads) {
  return (size + threads - 1) / threads;
}

// Reversed-Z orthographic projection: the near plane maps to depth 1.
glm::mat4 orthoReversedZ(float left, float right, float bottom, float top, float nearPlane, float farPlane) {
  return glm::orthoRH_ZO(left, right, bottom, top, farPlane, nearPlane);
}

} // namespace

Renderer::Renderer(rhi::IDevice &device, const std::filesystem::path &shaderDir, const RendererSettings &settings)
    : m_device(device), m_settings(settings) {
  const std::array cullModes{rhi::CullMode::Back, rhi::CullMode::None};
  for (std::size_t i = 0; i < 2; ++i) {
    const char *side = i == 0 ? "" : " double sided";
    defineGraphics(m_depthPipelines[i], "depth",
                   {.colorFormats = {},
                    .depthFormat = DepthFormat,
                    .depth = {.test = true, .write = true},
                    .cullMode = cullModes[i],
                    .debugName = std::format("depth{}", side)});
    // Shadows cull nothing: a single-sided ground plane has to cast its shadow too.
    defineGraphics(m_shadowPipelines[i], "depth",
                   {.vertexEntry = "shadowVertexMain",
                    .colorFormats = {},
                    .depthFormat = DepthFormat,
                    .depth = {.test = true, .write = true},
                    .cullMode = rhi::CullMode::None,
                    .debugName = "shadow"});
    // Equal against the pre-pass's depth: exactly the visible surface is shaded once.
    defineGraphics(m_forwardPipelines[i], "forward",
                   {.colorFormats = {HdrFormat},
                    .depthFormat = DepthFormat,
                    .depth = {.test = true, .write = false, .compare = rhi::CompareOp::Equal},
                    .cullMode = cullModes[i],
                    .debugName = std::format("forward{}", side)});
    defineGraphics(m_blendPipelines[i], "forward",
                   {.colorFormats = {HdrFormat},
                    .depthFormat = DepthFormat,
                    .depth = {.test = true, .write = false},
                    .cullMode = cullModes[i],
                    .blend = rhi::BlendMode::Alpha,
                    .debugName = std::format("forward blend{}", side)});
    // Reversed-Z GreaterOrEqual against the scene's depth keeps exactly the visible surface.
    defineGraphics(m_idPipelines[i], "id",
                   {.colorFormats = {IdFormat},
                    .depthFormat = DepthFormat,
                    .depth = {.test = true, .write = false},
                    .cullMode = cullModes[i],
                    .debugName = std::format("id{}", side)});
    // The same id shader without a depth attachment: every selected surface, occluded or not.
    defineGraphics(
        m_maskPipelines[i], "id",
        {.colorFormats = {IdFormat}, .cullMode = cullModes[i], .debugName = std::format("selection mask{}", side)});
  }
  // The skybox covers the pixels the pre-pass left at the far plane, depth 0.
  defineGraphics(m_skyboxPipeline, "skybox",
                 {.colorFormats = {HdrFormat},
                  .depthFormat = DepthFormat,
                  .depth = {.test = true, .write = false},
                  .cullMode = rhi::CullMode::None,
                  .debugName = "skybox"});
  defineGraphics(m_bloomDownPipeline, "post",
                 {.fragmentEntry = "bloomDownsample",
                  .colorFormats = {HdrFormat},
                  .cullMode = rhi::CullMode::None,
                  .debugName = "bloom downsample"});
  defineGraphics(m_bloomUpPipeline, "post",
                 {.fragmentEntry = "bloomUpsample",
                  .colorFormats = {HdrFormat},
                  .cullMode = rhi::CullMode::None,
                  .debugName = "bloom upsample"});
  defineGraphics(m_tonemapPipeline, "post",
                 {.fragmentEntry = "tonemap",
                  .colorFormats = {ColorFormat},
                  .cullMode = rhi::CullMode::None,
                  .debugName = "tonemap"});
  defineGraphics(
      m_fxaaPipeline, "post",
      {.fragmentEntry = "fxaa", .colorFormats = {ColorFormat}, .cullMode = rhi::CullMode::None, .debugName = "fxaa"});
  if (m_settings.presentFormat != rhi::Format::Undefined) {
    defineGraphics(m_presentPipeline, "post",
                   {.fragmentEntry = "blit",
                    .colorFormats = {m_settings.presentFormat},
                    .cullMode = rhi::CullMode::None,
                    .debugName = "present"});
  }
  defineGraphics(m_outlinePipeline, "outline",
                 {.colorFormats = {ColorFormat}, .cullMode = rhi::CullMode::None, .debugName = "outline"});
  defineGraphics(m_debugLinePipeline, "debug",
                 {.colorFormats = {ColorFormat},
                  .depthFormat = DepthFormat,
                  .depth = {.test = true, .write = false},
                  .cullMode = rhi::CullMode::None,
                  .topology = rhi::Topology::LineList,
                  .debugName = "debug lines"});
  // Particles: depth-tested against the scene, not written, blended into the HDR target.
  for (std::size_t blend = 0; blend < 2; ++blend) {
    defineGraphics(m_particleDrawPipelines[blend], "particles",
                   {.vertexEntry = "drawMain",
                    .colorFormats = {HdrFormat},
                    .depthFormat = DepthFormat,
                    .depth = {.test = true, .write = false},
                    .cullMode = rhi::CullMode::None,
                    .blend = blend == 0 ? rhi::BlendMode::Alpha : rhi::BlendMode::Additive,
                    .debugName = blend == 0 ? "particles alpha" : "particles additive"});
  }
  defineCompute(m_particleResetPipeline, "particles", "resetCommand", "particle reset");
  defineCompute(m_particleSimulatePipeline, "particles", "simulate", "particle simulation");
  defineCompute(m_clusterPipeline, "cluster", "computeMain", "light clustering");
  defineCompute(m_cullPipeline, "cull", "computeMain", "cull");
  defineCompute(m_clearCommandsPipeline, "cull", "clearCommands", "clear draw commands");
  defineCompute(m_equirectPipeline, "ibl", "equirectToCube", "equirect to cube");
  defineCompute(m_cubeMipPipeline, "ibl", "cubeMip", "cube mip");
  defineCompute(m_irradiancePipeline, "ibl", "irradiance", "irradiance");
  defineCompute(m_prefilterPipeline, "ibl", "prefilter", "prefilter");
  defineCompute(m_brdfLutPipeline, "ibl", "brdfLut", "brdf lut");
  defineCompute(m_skinPipeline, "skin", "computeMain", "skinning");
  createPipelines(shaderDir);
  createDefaults();
  SONNET_LOG_DEBUG("renderer ready, shaders from {}", shaderDir.string());
}

Renderer::~Renderer() {
  releaseSkinnedVertices(true);
  releaseParticles(true);
  if (m_particleQuad) {
    m_device.destroyBuffer(m_particleQuad);
  }
  m_meshes.forEach([this](MeshHandle handle, Mesh &mesh) {
    SONNET_LOG_WARN("leaked mesh \"{}\" ({}:{})", mesh.debugName, handle.index, handle.generation);
    m_device.destroyBuffer(mesh.vertices);
    m_device.destroyBuffer(mesh.indices);
    if (mesh.skin) {
      m_device.destroyBuffer(mesh.skin);
    }
    if (mesh.morph) {
      m_device.destroyBuffer(mesh.morph);
    }
  });
  m_environments.forEach([this](EnvironmentHandle handle, Environment &environment) {
    SONNET_LOG_WARN("leaked environment \"{}\" ({}:{})", environment.debugName, handle.index, handle.generation);
    destroyEnvironment(handle);
  });
  destroyTexture(m_flatNormalTexture);
  destroyTexture(m_whiteTexture);
  m_textures.forEach([this](TextureHandle handle, Texture &texture) {
    SONNET_LOG_WARN("leaked texture \"{}\" ({}:{})", texture.debugName, handle.index, handle.generation);
    m_device.destroyImage(texture.image);
  });
  m_device.destroyBuffer(m_clusterBuffer);
  if (m_commandBuffer) {
    m_device.destroyBuffer(m_commandBuffer);
  }
  if (m_visibleBuffer) {
    m_device.destroyBuffer(m_visibleBuffer);
  }
  m_device.destroyImage(m_brdfLut);
  m_device.destroySampler(m_shadowSampler);
  m_device.destroySampler(m_linearClampSampler);
  for (const rhi::SamplerHandle sampler : m_materialSamplers) {
    m_device.destroySampler(sampler);
  }
  for (const rhi::PipelineHandle pipeline : {m_skinPipeline,
                                             m_particleResetPipeline,
                                             m_particleSimulatePipeline,
                                             m_particleDrawPipelines[0],
                                             m_particleDrawPipelines[1],
                                             m_brdfLutPipeline,
                                             m_prefilterPipeline,
                                             m_irradiancePipeline,
                                             m_cubeMipPipeline,
                                             m_equirectPipeline,
                                             m_clearCommandsPipeline,
                                             m_cullPipeline,
                                             m_clusterPipeline,
                                             m_debugLinePipeline,
                                             m_outlinePipeline,
                                             m_fxaaPipeline,
                                             m_presentPipeline,
                                             m_tonemapPipeline,
                                             m_bloomUpPipeline,
                                             m_bloomDownPipeline,
                                             m_skyboxPipeline}) {
    // The present pipeline is there only when the settings asked for it.
    if (pipeline) {
      m_device.destroyPipeline(pipeline);
    }
  }
  for (const auto &pair :
       {m_maskPipelines, m_idPipelines, m_blendPipelines, m_forwardPipelines, m_shadowPipelines, m_depthPipelines}) {
    m_device.destroyPipeline(pair[1]);
    m_device.destroyPipeline(pair[0]);
  }
}

std::span<const std::string_view> Renderer::shaderNames() noexcept {
  static constexpr std::array<std::string_view, 12> Names{
      "cluster", "cull", "debug", "depth", "forward", "ibl", "id", "outline", "particles", "post", "skin", "skybox"};
  return Names;
}

void Renderer::defineGraphics(rhi::PipelineHandle &target, std::string shader, rhi::GraphicsPipelineDesc desc) {
  m_pipelineSlots.push_back(PipelineSlot{std::move(shader), std::move(desc), &target});
}

void Renderer::defineCompute(rhi::PipelineHandle &target, std::string shader, const char *entry, const char *name) {
  m_pipelineSlots.push_back(
      PipelineSlot{std::move(shader), rhi::ComputePipelineDesc{.entry = entry, .debugName = name}, &target});
}

rhi::PipelineHandle Renderer::createPipeline(const PipelineSlot &slot, rhi::ShaderHandle shader) {
  return std::visit(
      [&](auto desc) {
        desc.shader = shader;
        if constexpr (std::is_same_v<decltype(desc), rhi::GraphicsPipelineDesc>) {
          return m_device.createGraphicsPipeline(desc);
        } else {
          return m_device.createComputePipeline(desc);
        }
      },
      slot.desc);
}

void Renderer::createPipelines(const std::filesystem::path &shaderDir) {
  for (const std::string_view name : shaderNames()) {
    // Through the platform's content, so a relative shaderDir is read from the APK on Android.
    auto stream = platform::Platform::openContent(shaderDir / std::format("{}.spv", name));
    const auto spirv = stream ? stream->readAll() : std::unexpected(stream.error());
    if (!spirv) {
      throw core::Exception{spirv.error()};
    }
    // Logged before each creation, so the last line names the module or pipeline a driver crashed in.
    SONNET_LOG_DEBUG("shader module {}: {} bytes", name, spirv->size());
    const rhi::ShaderHandle shader = m_device.createShader({.spirv = *spirv, .debugName = std::string{name}});
    for (const PipelineSlot &slot : m_pipelineSlots) {
      if (slot.shader == name) {
        SONNET_LOG_DEBUG("pipeline \"{}\" from {}",
                         std::visit([](const auto &desc) -> const std::string & { return desc.debugName; }, slot.desc),
                         name);
        *slot.target = createPipeline(slot, shader);
      }
    }
    m_device.destroyShader(shader);
  }
}

core::Result<void> Renderer::reloadShader(std::string_view name, std::span<const std::byte> spirv) {
  if (std::ranges::find(shaderNames(), name) == shaderNames().end()) {
    return std::unexpected(core::Error{std::format("{} is not an engine shader", name), core::ErrorCategory::Shader});
  }
  // Everything new is built before anything old goes, so a rejected module or pipeline changes
  // nothing; the old pipelines are destroyed deferred, past the frames that still bind them.
  std::vector<std::pair<PipelineSlot *, rhi::PipelineHandle>> rebuilt;
  rhi::ShaderHandle shader;
  try {
    shader = m_device.createShader({.spirv = spirv, .debugName = std::string{name}});
    for (PipelineSlot &slot : m_pipelineSlots) {
      if (slot.shader == name) {
        rebuilt.emplace_back(&slot, createPipeline(slot, shader));
      }
    }
  } catch (const core::Exception &exception) {
    for (const auto &[slot, pipeline] : rebuilt) {
      m_device.destroyPipeline(pipeline);
    }
    if (shader) {
      m_device.destroyShader(shader);
    }
    return std::unexpected(exception.error());
  }
  m_device.destroyShader(shader);
  for (const auto &[slot, pipeline] : rebuilt) {
    m_device.destroyPipeline(*slot->target);
    *slot->target = pipeline;
  }
  SONNET_LOG_INFO("reloaded shader {}: {} pipelines rebuilt", name, rebuilt.size());
  return {};
}

void Renderer::createDefaults() {
  const std::array<rhi::AddressMode, 3> modes{rhi::AddressMode::Repeat, rhi::AddressMode::ClampToEdge,
                                              rhi::AddressMode::MirroredRepeat};
  const std::array<const char *, 3> names{"material repeat", "material clamp", "material mirror"};
  for (std::size_t i = 0; i < 3; ++i) {
    m_materialSamplers[i] =
        m_device.createSampler({.addressMode = modes[i], .anisotropy = 8.0f, .debugName = names[i]});
  }
  m_linearClampSampler =
      m_device.createSampler({.addressMode = rhi::AddressMode::ClampToEdge, .debugName = "linear clamp"});
  m_shadowSampler =
      m_device.createSampler({.addressMode = rhi::AddressMode::ClampToEdge, .compare = true, .debugName = "shadow"});
  m_whiteTexture = createTexture(solidTexture({255, 255, 255, 255}), "white");
  m_flatNormalTexture = createTexture(solidTexture({128, 128, 255, 255}), "flat normal");
  m_brdfLut = m_device.createImage({.size = {m_settings.brdfLutSize, m_settings.brdfLutSize},
                                    .format = HdrFormat,
                                    .usage = rhi::ImageUsage::Sampled | rhi::ImageUsage::Storage,
                                    .debugName = "brdf lut"});
  const std::array<std::uint32_t, 6> quad{0, 1, 2, 2, 1, 3};
  m_particleQuad = m_device.createBuffer({.size = sizeof(quad),
                                          .usage = rhi::BufferUsage::Index | rhi::BufferUsage::TransferDst,
                                          .debugName = "particle quad"});
  m_device.uploadBuffer(m_particleQuad, 0, std::as_bytes(std::span{quad}));
  m_clusterBuffer = m_device.createBuffer({.size = std::uint64_t{ClusterCount} * ClusterBytes,
                                           .usage = rhi::BufferUsage::Storage,
                                           .debugName = "light clusters"});
}

// ---- Meshes ----

MeshHandle Renderer::createMesh(const MeshData &data, std::string debugName) {
  SONNET_ASSERT(!data.vertices.empty() && data.indices.size() % 3 == 0, "mesh \"{}\" is not a triangle list",
                debugName);
  const std::uint64_t vertexBytes = data.vertices.size() * sizeof(Vertex);
  const std::uint64_t indexBytes = data.indices.size() * sizeof(std::uint32_t);
  const rhi::BufferHandle vertices =
      m_device.createBuffer({.size = vertexBytes,
                             .usage = rhi::BufferUsage::Storage | rhi::BufferUsage::TransferDst,
                             .debugName = std::format("{} vertices", debugName)});
  const rhi::BufferHandle indices =
      m_device.createBuffer({.size = indexBytes,
                             .usage = rhi::BufferUsage::Index | rhi::BufferUsage::TransferDst,
                             .debugName = std::format("{} indices", debugName)});
  m_device.uploadBuffer(vertices, 0, std::as_bytes(std::span{data.vertices}));
  m_device.uploadBuffer(indices, 0, std::as_bytes(std::span{data.indices}));
  rhi::BufferHandle morph;
  std::uint32_t morphTargets = 0;
  if (data.morphTargetCount > 0 &&
      data.morphDeltas.size() == std::size_t{data.morphTargetCount} * data.vertices.size()) {
    morphTargets = std::min(data.morphTargetCount, MaxMorphTargets);
    // Only the targets that are kept: they are the first in the array.
    const std::span<const MorphDelta> kept =
        std::span{data.morphDeltas}.first(std::size_t{morphTargets} * data.vertices.size());
    morph = m_device.createBuffer({.size = kept.size_bytes(),
                                   .usage = rhi::BufferUsage::Storage | rhi::BufferUsage::TransferDst,
                                   .debugName = std::format("{} morph", debugName)});
    m_device.uploadBuffer(morph, 0, std::as_bytes(kept));
  } else if (data.morphTargetCount > 0) {
    SONNET_LOG_ERROR("mesh \"{}\": {} morph deltas for {} targets of {} vertices, drawn unmorphed", debugName,
                     data.morphDeltas.size(), data.morphTargetCount, data.vertices.size());
  }
  rhi::BufferHandle skin;
  if (!data.skin.empty() && data.skin.size() != data.vertices.size()) {
    SONNET_LOG_ERROR("mesh \"{}\": {} skin weights for {} vertices, drawn unskinned", debugName, data.skin.size(),
                     data.vertices.size());
  } else if (!data.skin.empty()) {
    skin = m_device.createBuffer({.size = data.skin.size() * sizeof(SkinWeights),
                                  .usage = rhi::BufferUsage::Storage | rhi::BufferUsage::TransferDst,
                                  .debugName = std::format("{} skin", debugName)});
    m_device.uploadBuffer(skin, 0, std::as_bytes(std::span{data.skin}));
  }
  std::vector<Submesh> submeshes = data.submeshes;
  if (submeshes.empty()) {
    submeshes.push_back(Submesh{0, static_cast<std::uint32_t>(data.indices.size()), 0});
  }
  const MeshHandle handle =
      m_meshes.emplace(Mesh{std::move(debugName), vertices, indices, skin, morph, morphTargets,
                            static_cast<std::uint32_t>(data.vertices.size()), std::move(submeshes), data.bounds()});
  SONNET_LOG_DEBUG("mesh \"{}\": {} vertices, {} triangles, {} submeshes", m_meshes.get(handle).debugName,
                   data.vertices.size(), data.triangleCount(), m_meshes.get(handle).submeshes.size());
  return handle;
}

void Renderer::destroyMesh(MeshHandle handle) {
  std::optional<Mesh> mesh = m_meshes.remove(handle);
  if (!mesh) {
    SONNET_LOG_WARN("destroyMesh: stale handle {}:{}", handle.index, handle.generation);
    return;
  }
  m_device.destroyBuffer(mesh->vertices);
  m_device.destroyBuffer(mesh->indices);
  if (mesh->skin) {
    m_device.destroyBuffer(mesh->skin);
  }
  if (mesh->morph) {
    m_device.destroyBuffer(mesh->morph);
  }
}

bool Renderer::isValid(MeshHandle handle) const {
  return m_meshes.contains(handle);
}

std::span<const Submesh> Renderer::submeshes(MeshHandle handle) const {
  const Mesh *mesh = m_meshes.find(handle);
  return mesh != nullptr ? std::span<const Submesh>{mesh->submeshes} : std::span<const Submesh>{};
}

Bounds Renderer::meshBounds(MeshHandle handle) const {
  const Mesh *mesh = m_meshes.find(handle);
  return mesh != nullptr ? mesh->bounds : Bounds{};
}

// ---- Textures ----

TextureHandle Renderer::createTexture(const TextureData &data, std::string debugName) {
  if (data.data.size() != data.expectedSize() || data.mipLevels == 0) {
    SONNET_LOG_ERROR("texture \"{}\": {} bytes given, {} expected for {}x{} with {} levels", debugName,
                     data.data.size(), data.expectedSize(), data.size.x, data.size.y, data.mipLevels);
    return {};
  }
  const rhi::ImageHandle image = m_device.createImage({.size = data.size,
                                                       .format = data.format,
                                                       .usage = rhi::ImageUsage::Sampled | rhi::ImageUsage::TransferDst,
                                                       .mipLevels = data.mipLevels,
                                                       .cube = data.cube,
                                                       .debugName = debugName});
  std::vector<rhi::ImageUpload> uploads;
  uploads.reserve(std::size_t{data.mipLevels} * data.layers());
  for (std::uint32_t level = 0; level < data.mipLevels; ++level) {
    for (std::uint32_t layer = 0; layer < data.layers(); ++layer) {
      uploads.push_back({.mipLevel = level, .layer = layer, .data = data.level(level, layer)});
    }
  }
  m_device.uploadImage(image, uploads);
  const TextureHandle handle = m_textures.emplace(Texture{std::move(debugName), image});
  SONNET_LOG_DEBUG("texture \"{}\": {}x{} {}, {} levels{}", m_textures.get(handle).debugName, data.size.x, data.size.y,
                   rhi::toString(data.format), data.mipLevels, data.cube ? ", cube" : "");
  return handle;
}

void Renderer::destroyTexture(TextureHandle handle) {
  std::optional<Texture> texture = m_textures.remove(handle);
  if (!texture) {
    SONNET_LOG_WARN("destroyTexture: stale handle {}:{}", handle.index, handle.generation);
    return;
  }
  m_device.destroyImage(texture->image);
}

bool Renderer::isValid(TextureHandle handle) const {
  return m_textures.contains(handle);
}

std::uint32_t Renderer::textureIndex(TextureHandle handle) const {
  const Texture *texture = m_textures.find(handle);
  if (texture == nullptr) {
    texture = m_textures.find(m_whiteTexture);
  }
  return texture != nullptr ? m_device.sampledImageIndex(texture->image) : rhi::InvalidBindlessIndex;
}

std::uint32_t Renderer::sampledIndex(rhi::ImageHandle image) const {
  return m_device.sampledImageIndex(image);
}

// ---- Materials ----

MaterialHandle Renderer::createMaterial(const MaterialDesc &desc, std::string debugName) {
  const MaterialHandle handle = m_materials.emplace(Material{std::move(debugName), desc});
  m_materialSlots = std::max(m_materialSlots, handle.index + 1);
  return handle;
}

void Renderer::updateMaterial(MaterialHandle handle, const MaterialDesc &desc) {
  Material *material = m_materials.find(handle);
  if (material == nullptr) {
    SONNET_LOG_WARN("updateMaterial: stale handle {}:{}", handle.index, handle.generation);
    return;
  }
  material->desc = desc;
}

void Renderer::destroyMaterial(MaterialHandle handle) {
  if (!m_materials.remove(handle)) {
    SONNET_LOG_WARN("destroyMaterial: stale handle {}:{}", handle.index, handle.generation);
  }
}

bool Renderer::isValid(MaterialHandle handle) const {
  return m_materials.contains(handle);
}

const MaterialDesc &Renderer::material(MaterialHandle handle) const {
  return m_materials.get(handle).desc;
}

std::uint32_t Renderer::materialIndex(MaterialHandle handle) const {
  // Slot 0 of the GPU array is the default material; live materials follow by pool index.
  return m_materials.contains(handle) ? handle.index + 1 : 0;
}

// ---- Environments ----

EnvironmentHandle Renderer::createEnvironment(const TextureData &equirectangular, std::string debugName) {
  if (equirectangular.data.size() != equirectangular.expectedSize() || equirectangular.cube) {
    SONNET_LOG_ERROR("environment \"{}\": the equirectangular map is not a 2D image with {} bytes", debugName,
                     equirectangular.expectedSize());
    return {};
  }
  const rhi::ImageHandle equirect =
      m_device.createImage({.size = equirectangular.size,
                            .format = equirectangular.format,
                            .usage = rhi::ImageUsage::Sampled | rhi::ImageUsage::TransferDst,
                            .mipLevels = equirectangular.mipLevels,
                            .debugName = std::format("{} equirectangular", debugName)});
  std::vector<rhi::ImageUpload> uploads;
  uploads.reserve(equirectangular.mipLevels);
  for (std::uint32_t level = 0; level < equirectangular.mipLevels; ++level) {
    uploads.push_back({.mipLevel = level, .layer = 0, .data = equirectangular.level(level)});
  }
  m_device.uploadImage(equirect, uploads);
  const rhi::ImageUsage cubeUsage = rhi::ImageUsage::Sampled | rhi::ImageUsage::Storage;
  const rhi::ImageHandle skybox =
      m_device.createImage({.size = {m_settings.environmentSize, m_settings.environmentSize},
                            .format = HdrFormat,
                            .usage = cubeUsage,
                            .mipLevels = rhi::fullMipCount({m_settings.environmentSize, m_settings.environmentSize}),
                            .cube = true,
                            .debugName = std::format("{} skybox", debugName)});
  const rhi::ImageHandle irradiance =
      m_device.createImage({.size = {m_settings.irradianceSize, m_settings.irradianceSize},
                            .format = HdrFormat,
                            .usage = cubeUsage,
                            .cube = true,
                            .debugName = std::format("{} irradiance", debugName)});
  const rhi::ImageHandle prefiltered =
      m_device.createImage({.size = {m_settings.prefilteredSize, m_settings.prefilteredSize},
                            .format = HdrFormat,
                            .usage = cubeUsage,
                            .mipLevels = m_settings.prefilteredLevels,
                            .cube = true,
                            .debugName = std::format("{} prefiltered", debugName)});
  const EnvironmentHandle handle =
      m_environments.emplace(Environment{std::move(debugName), equirect, skybox, irradiance, prefiltered, true});
  SONNET_LOG_DEBUG("environment \"{}\" from a {}x{} map", m_environments.get(handle).debugName, equirectangular.size.x,
                   equirectangular.size.y);
  return handle;
}

void Renderer::destroyEnvironment(EnvironmentHandle handle) {
  std::optional<Environment> environment = m_environments.remove(handle);
  if (!environment) {
    SONNET_LOG_WARN("destroyEnvironment: stale handle {}:{}", handle.index, handle.generation);
    return;
  }
  if (environment->equirectangular) {
    m_device.destroyImage(environment->equirectangular);
  }
  m_device.destroyImage(environment->prefiltered);
  m_device.destroyImage(environment->irradiance);
  m_device.destroyImage(environment->skybox);
}

bool Renderer::isValid(EnvironmentHandle handle) const {
  return m_environments.contains(handle);
}

bool Renderer::isReady(EnvironmentHandle handle) const {
  const Environment *environment = m_environments.find(handle);
  return environment != nullptr && !environment->pending;
}

// ---- Precomputation ----

void Renderer::addPrecomputePasses(RenderGraph &graph, const SceneView &view) {
  // The lookup table and the view's environment are imported once per frame: computed by the
  // passes below when pending, and sampled by the forward pass through these images either
  // way, so the graph orders the reads after the writes. A second view of the frame takes the
  // first's imports rather than importing the images again as if nothing were writing them.
  if (m_importSerial != graph.frameSerial()) {
    m_importSerial = graph.frameSerial();
    m_environmentImports.clear();
    m_lutImport = graph.importImage(m_brdfLut, rhi::ImageLayout::General,
                                    m_brdfLutPending ? rhi::ImageLayout::Undefined : rhi::ImageLayout::General);
  }
  m_frameImages = {};
  m_frameImages.lut = m_lutImport;
  if (const Environment *environment = m_environments.find(view.environment)) {
    const EnvironmentImport imports = importEnvironment(graph, view.environment, *environment);
    m_frameImages.skybox = imports.skybox;
    m_frameImages.irradiance = imports.irradiance;
    m_frameImages.prefiltered = imports.prefiltered;
  }
  if (m_brdfLutPending) {
    m_brdfLutPending = false;
    const GraphImage lut = m_frameImages.lut;
    graph.addPass(
        "brdf lut", [&](PassBuilder &builder) { builder.storage(lut); },
        [this](rhi::ICommandList &commands, const PassResources &) {
          const IblConstants push{.source = rhi::InvalidBindlessIndex,
                                  .destination = m_device.storageImageIndex(m_brdfLut, 0),
                                  .size = m_settings.brdfLutSize,
                                  .sampleCount = m_settings.brdfLutSamples,
                                  .roughness = 0.0f,
                                  .sourceLod = 0.0f,
                                  .sampler = m_device.samplerIndex(m_linearClampSampler)};
          commands.bindPipeline(m_brdfLutPipeline);
          commands.pushConstants(std::as_bytes(std::span{&push, 1}));
          commands.dispatch(groups(m_settings.brdfLutSize, 8), groups(m_settings.brdfLutSize, 8), 1);
        });
  }
  m_environments.forEach([&](EnvironmentHandle handle, Environment &environment) {
    if (environment.pending) {
      addEnvironmentPasses(graph, handle, environment);
    }
  });
}

Renderer::EnvironmentImport Renderer::importEnvironment(RenderGraph &graph, EnvironmentHandle handle,
                                                        const Environment &environment) {
  for (const EnvironmentImport &imported : m_environmentImports) {
    if (imported.handle == handle) {
      return imported;
    }
  }
  // Read before addEnvironmentPasses clears `pending`: a pending environment starts the frame
  // as garbage for its own passes to fill.
  const rhi::ImageLayout initial = environment.pending ? rhi::ImageLayout::Undefined : rhi::ImageLayout::General;
  const EnvironmentImport imports{
      .handle = handle,
      .skybox = graph.importImage(environment.skybox, rhi::ImageLayout::General, initial),
      .irradiance = graph.importImage(environment.irradiance, rhi::ImageLayout::General, initial),
      .prefiltered = graph.importImage(environment.prefiltered, rhi::ImageLayout::General, initial)};
  m_environmentImports.push_back(imports);
  return imports;
}

void Renderer::addEnvironmentPasses(RenderGraph &graph, EnvironmentHandle handle, Environment &environment) {
  const EnvironmentImport imports = importEnvironment(graph, handle, environment);
  environment.pending = false;
  const std::uint32_t sampler = m_device.samplerIndex(m_linearClampSampler);
  const GraphImage equirect = graph.importImage(environment.equirectangular, rhi::ImageLayout::ShaderReadOnly,
                                                rhi::ImageLayout::ShaderReadOnly);
  const GraphImage skybox = imports.skybox;
  const GraphImage irradiance = imports.irradiance;
  const GraphImage prefiltered = imports.prefiltered;
  const rhi::ImageHandle skyboxImage = environment.skybox;
  const rhi::ImageHandle irradianceImage = environment.irradiance;
  const rhi::ImageHandle prefilteredImage = environment.prefiltered;
  const rhi::ImageHandle equirectImage = environment.equirectangular;
  const std::uint32_t skyboxSize = m_settings.environmentSize;
  const std::uint32_t skyboxLevels = rhi::fullMipCount({skyboxSize, skyboxSize});

  const auto dispatchCube = [this, sampler](rhi::ICommandList &commands, rhi::PipelineHandle pipeline,
                                            std::uint32_t source, rhi::ImageHandle destination, std::uint32_t level,
                                            std::uint32_t size, std::uint32_t samples, float roughness,
                                            float sourceLod) {
    const IblConstants push{.source = source,
                            .destination = m_device.storageImageIndex(destination, level),
                            .size = size,
                            .sampleCount = samples,
                            .roughness = roughness,
                            .sourceLod = sourceLod,
                            .sampler = sampler};
    commands.bindPipeline(pipeline);
    commands.pushConstants(std::as_bytes(std::span{&push, 1}));
    commands.dispatch(groups(size, 8), groups(size, 8), 6);
  };

  graph.addPass(
      "equirect to cube",
      [&](PassBuilder &builder) {
        builder.sample(equirect, true);
        builder.storage(skybox);
      },
      [=, this](rhi::ICommandList &commands, const PassResources &) {
        dispatchCube(commands, m_equirectPipeline, sampledIndex(equirectImage), skyboxImage, 0, skyboxSize, 0, 0.0f,
                     0.0f);
      });
  for (std::uint32_t level = 1; level < skyboxLevels; ++level) {
    // Each level reads the previous one through the sampled view; the write-after-write on the
    // image between passes is the barrier that orders them.
    graph.addPass(
        std::format("cube mip {}", level), [&](PassBuilder &builder) { builder.storage(skybox); },
        [=, this](rhi::ICommandList &commands, const PassResources &) {
          dispatchCube(commands, m_cubeMipPipeline, sampledIndex(skyboxImage), skyboxImage, level,
                       rhi::mipSize({skyboxSize, skyboxSize}, level).x, 0, 0.0f, static_cast<float>(level - 1));
        });
  }
  // A blurred source level keeps the sample counts modest without noise.
  const float irradianceLod = std::max(0.0f, std::log2(static_cast<float>(skyboxSize) / 32.0f));
  graph.addPass(
      "irradiance",
      [&](PassBuilder &builder) {
        builder.sample(skybox, true);
        builder.storage(irradiance);
      },
      [=, this](rhi::ICommandList &commands, const PassResources &) {
        dispatchCube(commands, m_irradiancePipeline, sampledIndex(skyboxImage), irradianceImage, 0,
                     m_settings.irradianceSize, m_settings.irradianceSamples, 0.0f, irradianceLod);
      });
  for (std::uint32_t level = 0; level < m_settings.prefilteredLevels; ++level) {
    const float roughness = m_settings.prefilteredLevels > 1
                                ? static_cast<float>(level) / static_cast<float>(m_settings.prefilteredLevels - 1)
                                : 0.0f;
    const std::uint32_t size = rhi::mipSize({m_settings.prefilteredSize, m_settings.prefilteredSize}, level).x;
    graph.addPass(
        std::format("prefilter {}", level),
        [&](PassBuilder &builder) {
          builder.sample(skybox, true);
          builder.storage(prefiltered);
        },
        [=, this](rhi::ICommandList &commands, const PassResources &) {
          dispatchCube(commands, m_prefilterPipeline, sampledIndex(skyboxImage), prefilteredImage, level, size,
                       m_settings.prefilterSamples, roughness, 0.0f);
        });
  }
  // The map is no longer needed once the frame that reads it has been recorded; destruction
  // is deferred past that frame by the device.
  graph.addPass(
      "release equirect", [](PassBuilder &) {},
      [this, handle, equirectImage](rhi::ICommandList &, const PassResources &) {
        m_device.destroyImage(equirectImage);
        if (Environment *current = m_environments.find(handle)) {
          current->equirectangular = {};
        }
      });
}

// ---- Frame preparation ----

void Renderer::computeCascades(ViewState &v, float aspect) {
  const SceneView &view = *v.view;
  const float nearPlane = view.camera.nearPlane;
  const float farPlane = std::max(m_settings.shadowDistance, nearPlane * 2.0f);
  const glm::mat4 cameraView = view.camera.view();
  const glm::vec3 lightDirection = glm::normalize(view.sun.direction);
  const glm::vec3 up = std::abs(lightDirection.y) > 0.99f ? glm::vec3{0.0f, 0.0f, 1.0f} : glm::vec3{0.0f, 1.0f, 0.0f};
  constexpr float lambda = 0.75f; // between logarithmic and uniform splits
  float previousSplit = nearPlane;
  float sliceNear = nearPlane;
  for (std::uint32_t c = 0; c < CascadeCount; ++c) {
    const float p = static_cast<float>(c + 1) / static_cast<float>(CascadeCount);
    const float logarithmic = nearPlane * std::pow(farPlane / nearPlane, p);
    const float uniform = nearPlane + (farPlane - nearPlane) * p;
    const float split = lambda * logarithmic + (1.0f - lambda) * uniform;

    // The slice's eight corners in world space, from a finite projection over the slice.
    const glm::mat4 sliceProjection = glm::perspectiveRH_ZO(view.camera.fovY, aspect, sliceNear, split);
    const glm::mat4 inverse = glm::inverse(sliceProjection * cameraView);
    std::array<glm::vec3, 8> corners;
    std::size_t index = 0;
    for (const float z : {0.0f, 1.0f}) {
      for (const float y : {-1.0f, 1.0f}) {
        for (const float x : {-1.0f, 1.0f}) {
          const glm::vec4 world = inverse * glm::vec4{x, y, z, 1.0f};
          corners[index++] = glm::vec3{world} / world.w;
        }
      }
    }
    // A bounding sphere keeps the projection's size constant as the camera turns, and snapping
    // its centre to shadow texels stops the edges from shimmering as it moves.
    glm::vec3 centre{0.0f};
    for (const glm::vec3 &corner : corners) {
      centre += corner;
    }
    centre /= 8.0f;
    float radius = 0.0f;
    for (const glm::vec3 &corner : corners) {
      radius = std::max(radius, glm::length(corner - centre));
    }
    radius = std::ceil(radius * 16.0f) / 16.0f;
    // Leave room for the 3x3 bilinear filter and receiver offset after texel snapping.
    radius *= static_cast<float>(m_settings.shadowMapSize) /
              std::max(1.0f, static_cast<float>(m_settings.shadowMapSize) - 10.0f);
    const float extension = farPlane; // casters this far behind the slice still count
    const glm::mat4 lightView = glm::lookAt(centre - lightDirection * (radius + extension), centre, up);
    glm::mat4 lightProjection = orthoReversedZ(-radius, radius, -radius, radius, 0.0f, 2.0f * radius + extension);
    const float halfResolution = static_cast<float>(m_settings.shadowMapSize) * 0.5f;
    const glm::vec4 origin = lightProjection * lightView * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f};
    const glm::vec2 snapped = glm::round(glm::vec2{origin} * halfResolution) / halfResolution;
    const glm::vec2 offset = snapped - glm::vec2{origin};
    lightProjection[3][0] += offset.x;
    lightProjection[3][1] += offset.y;
    const float blendStart = split - (split - previousSplit) * 0.1f;
    v.cascades[c] = Cascade{lightProjection * lightView, split, blendStart,
                            2.0f * radius / static_cast<float>(m_settings.shadowMapSize)};
    sliceNear = blendStart;
    previousSplit = split;
  }
}

namespace {

// The lights that get shadow maps this frame, in the light list's order: the first ones that ask,
// up to the budget, so the same lights are chosen on every run. A spot light takes one map and a
// point light six; one whose maps no longer fit in the depth array is passed over, so a spot light
// after it can still be granted. The lights not granted illuminate without shadows.
template <typename Grant> void grantLocalShadows(const SceneView &view, std::uint32_t budget, Grant &&grant) {
  const auto lightCount = static_cast<std::uint32_t>(std::min<std::size_t>(view.lights.size(), Renderer::MaxLights));
  std::uint32_t lights = 0;
  std::uint32_t maps = 0;
  for (std::uint32_t i = 0; i < lightCount && lights < budget; ++i) {
    const Light &light = view.lights[i];
    if (!light.castsShadows) {
      continue;
    }
    const std::uint32_t faces = light.type == LightType::Point ? Renderer::PointShadowFaces : 1;
    if (maps + faces > Renderer::MaxLocalShadows) {
      continue;
    }
    grant(i, faces);
    ++lights;
    maps += faces;
  }
}

} // namespace

std::uint32_t Renderer::localShadowMapCount(const SceneView &view) const {
  std::uint32_t maps = 0;
  grantLocalShadows(view, localShadowBudget(), [&](std::uint32_t, std::uint32_t faces) { maps += faces; });
  return maps;
}

// Each spot light's frustum is its cone and each point light's six are the faces of a cube, all a
// little wider than that so the filter's footprint at the edge stays inside the map.
void Renderer::selectLocalShadows(ViewState &v) const {
  const SceneView &view = *v.view;
  const float size = static_cast<float>(m_settings.localShadowMapSize);
  const float margin = size / std::max(1.0f, size - 4.0f);
  grantLocalShadows(view, localShadowBudget(), [&](std::uint32_t index, std::uint32_t faces) {
    const Light &light = view.lights[index];
    const float nearPlane = std::clamp(light.range * 0.01f, 0.05f, 0.5f);
    if (light.type == LightType::Point) {
      static constexpr std::array<glm::vec3, PointShadowFaces> FaceDirections{
          glm::vec3{1.0f, 0.0f, 0.0f},  glm::vec3{-1.0f, 0.0f, 0.0f}, glm::vec3{0.0f, 1.0f, 0.0f},
          glm::vec3{0.0f, -1.0f, 0.0f}, glm::vec3{0.0f, 0.0f, 1.0f},  glm::vec3{0.0f, 0.0f, -1.0f}};
      const glm::mat4 projection = perspectiveReversedZ(2.0f * std::atan(margin), 1.0f, nearPlane);
      for (std::uint32_t face = 0; face < faces; ++face) {
        const glm::vec3 up = face == 2 || face == 3 ? glm::vec3{0.0f, 0.0f, 1.0f} : glm::vec3{0.0f, 1.0f, 0.0f};
        v.localShadows.push_back(
            LocalShadow{.light = index,
                        .face = face,
                        .matrix = projection * glm::lookAt(light.position, light.position + FaceDirections[face], up),
                        .image = {}});
      }
      return;
    }
    const glm::vec3 direction = glm::normalize(light.direction);
    const glm::vec3 up = std::abs(direction.y) > 0.99f ? glm::vec3{0.0f, 0.0f, 1.0f} : glm::vec3{0.0f, 1.0f, 0.0f};
    const float halfAngle = std::clamp(light.outerAngle, glm::radians(0.5f), glm::radians(80.0f));
    const float fovY = 2.0f * std::atan(std::tan(halfAngle) * margin);
    v.localShadows.push_back(LocalShadow{.light = index,
                                         .face = 0,
                                         .matrix = perspectiveReversedZ(fovY, 1.0f, nearPlane) *
                                                   glm::lookAt(light.position, light.position + direction, up),
                                         .image = {}});
  });
}

void Renderer::parallelFor(const char *name, std::size_t count, std::size_t grain,
                           const std::function<void(std::size_t, std::size_t)> &body) const {
  if (count == 0) {
    return;
  }
  if (m_settings.jobs == nullptr) {
    body(0, count);
    return;
  }
  m_settings.jobs->parallelFor(name, count, grain, body);
}

Renderer::ViewState &Renderer::prepareFrame(RenderGraph &graph, const SceneView &view, glm::uvec2 targetSize) {
  SONNET_ZONE();
  // A new graph frame starts the views over and shares the two cull buffers out afresh. The
  // serial rather than the graph's address and frame count, which a graph built where an earlier
  // one stood would repeat.
  if (m_graphSerial != graph.frameSerial() || m_viewCount == 0) {
    m_graphSerial = graph.frameSerial();
    m_graphFrame = graph.frameIndex();
    m_viewCount = 0;
    m_commandsReserved = 0;
    m_visibleReserved = 0;
  }
  // Once per view, however many passes ask.
  for (std::size_t i = 0; i < m_viewCount; ++i) {
    if (m_views[i]->view == &view && m_views[i]->targetSize == targetSize) {
      return *m_views[i];
    }
  }
  if (m_viewCount == m_views.size()) {
    m_views.push_back(std::make_unique<ViewState>());
  }
  ViewState &v = *m_views[m_viewCount++];
  v.view = &view;
  v.targetSize = targetSize;
  v.cascadesActive = false;
  v.localShadows.clear();
  v.frameBuffers = {};
  v.statistics = {};
  v.resolved.clear();
  v.skinJobs.clear();
  v.particleJobs.clear();
  v.opaqueOrder.clear();
  v.blendedOrder.clear();
  v.allOrder.clear();
  v.opaqueBatches.clear();
  v.allBatches.clear();
  v.cullJobs.clear();
  v.opaqueJobsUsed = 0;
  v.allJobsUsed = 0;
  v.firstPendingJob = 0;
  const glm::mat4 cameraView = view.camera.view();
  for (std::size_t i = 0; i < view.draws.size(); ++i) {
    const DrawItem &item = view.draws[i];
    const Mesh *mesh = m_meshes.find(item.mesh);
    if (mesh == nullptr || item.submesh >= mesh->submeshes.size()) {
      continue; // a stale handle draws nothing; the owner is expected to notice
    }
    const Material *material = m_materials.find(item.material);
    const MaterialDesc &desc = material != nullptr ? material->desc : MaterialDesc{};
    const glm::vec4 viewPosition = cameraView * item.transform[3];
    // The world-space box the culling pass tests: the mesh's bounds through the transform, the
    // eight corners rather than the transformed extent so a rotation stays conservative. A
    // skinned draw is culled by its bind pose, which ADR-0012 accepts.
    glm::vec3 minimum{std::numeric_limits<float>::max()};
    glm::vec3 maximum{std::numeric_limits<float>::lowest()};
    for (int corner = 0; corner < 8; ++corner) {
      const glm::vec3 local{(corner & 1) != 0 ? mesh->bounds.max.x : mesh->bounds.min.x,
                            (corner & 2) != 0 ? mesh->bounds.max.y : mesh->bounds.min.y,
                            (corner & 4) != 0 ? mesh->bounds.max.z : mesh->bounds.min.z};
      const glm::vec3 world{item.transform * glm::vec4{local, 1.0f}};
      minimum = glm::min(minimum, world);
      maximum = glm::max(maximum, world);
    }
    const std::uint32_t vertexBuffer = resolveVertices(v, item, *mesh);
    if (vertexBuffer == rhi::InvalidBindlessIndex) {
      continue; // the bindless vertex-buffer array is full, which the device has logged
    }
    v.resolved.push_back(ResolvedDraw{.objectIndex = static_cast<std::uint32_t>(i),
                                      .mesh = mesh,
                                      .vertexBuffer = vertexBuffer,
                                      .submesh = mesh->submeshes[item.submesh],
                                      .center = (minimum + maximum) * 0.5f,
                                      .extent = (maximum - minimum) * 0.5f,
                                      .doubleSided = desc.doubleSided,
                                      .mirrored = glm::determinant(glm::mat3{item.transform}) < 0.0f,
                                      .blended = desc.alphaMode == AlphaMode::Blend,
                                      .masked = desc.alphaMode == AlphaMode::Mask,
                                      .viewDepth = -viewPosition.z});
  }
  for (std::uint32_t i = 0; i < v.resolved.size(); ++i) {
    v.allOrder.push_back(i);
    (v.resolved[i].blended ? v.blendedOrder : v.opaqueOrder).push_back(i);
  }
  // Opaque draws grouped by pipeline, then by front face, then by mesh so index buffers stay
  // bound, then by submesh so a batch's instances share an index range (ADR-0016); blended ones
  // from the farthest to the nearest.
  const auto byBatch = [&](std::uint32_t a, std::uint32_t b) {
    const ResolvedDraw &da = v.resolved[a];
    const ResolvedDraw &db = v.resolved[b];
    if (da.doubleSided != db.doubleSided) {
      return !da.doubleSided;
    }
    if (da.mirrored != db.mirrored) {
      return !da.mirrored;
    }
    if (da.mesh != db.mesh) {
      return da.mesh < db.mesh;
    }
    if (da.submesh.firstIndex != db.submesh.firstIndex) {
      return da.submesh.firstIndex < db.submesh.firstIndex;
    }
    return da.submesh.indexCount < db.submesh.indexCount;
  };
  std::ranges::sort(v.opaqueOrder, byBatch);
  std::ranges::sort(v.blendedOrder, [&](std::uint32_t a, std::uint32_t b) {
    return v.resolved[a].viewDepth > v.resolved[b].viewDepth;
  });
  // The id and mask passes do not care about order, so grouping them the same way turns them
  // into batches too.
  std::ranges::sort(v.allOrder, byBatch);
  buildBatches(v, v.opaqueOrder, v.opaqueBatches);
  buildBatches(v, v.allOrder, v.allBatches);
  ensureIndirectBuffers(v);

  releaseSkinnedVertices(false);
  releaseParticles(false);
  if (!v.skinJobs.empty()) {
    graph.addPass(
        "skinning", [](PassBuilder &) {},
        [this, &v](rhi::ICommandList &commands, const PassResources &) { recordSkinning(commands, v); });
  }
  return v;
}

void Renderer::buildBatches(const ViewState &v, std::span<const std::uint32_t> order,
                            std::vector<Batch> &batches) const {
  batches.clear();
  for (std::uint32_t position = 0; position < order.size(); ++position) {
    const ResolvedDraw &draw = v.resolved[order[position]];
    const std::uint32_t pipeline = draw.doubleSided ? 1u : 0u;
    if (!batches.empty()) {
      Batch &last = batches.back();
      if (last.mesh == draw.mesh && last.submesh.firstIndex == draw.submesh.firstIndex &&
          last.submesh.indexCount == draw.submesh.indexCount && last.pipeline == pipeline &&
          last.mirrored == draw.mirrored) {
        ++last.drawCount;
        continue;
      }
    }
    batches.push_back(Batch{.mesh = draw.mesh,
                            .submesh = draw.submesh,
                            .pipeline = pipeline,
                            .mirrored = draw.mirrored,
                            .firstDraw = position,
                            .drawCount = 1});
  }
}

void Renderer::ensureIndirectBuffers(ViewState &v) {
  // One command per batch per job, and a visible-list slot per draw per job, since culling is
  // what decides how many of a batch's slots its instances fill (ADR-0016). The direct range
  // after the jobs' runs gives every resolved draw a slot the direct path can name. Each view of
  // the graph frame takes a slice after the ones before it, so views never share a command or a
  // slot, and the buffers grow to the sum of the slices (docs/decisions/0021-two-views-in-one-frame.md).
  const auto opaqueDraws = static_cast<std::uint32_t>(v.opaqueOrder.size());
  const auto allDraws = static_cast<std::uint32_t>(v.allOrder.size());
  const std::uint32_t localMaps = m_settings.shadows && !v.opaqueOrder.empty() ? localShadowMapCount(*v.view) : 0;
  v.opaqueJobCapacity = CullJobsOpaque + localMaps;
  const std::uint32_t commands = static_cast<std::uint32_t>(v.opaqueBatches.size()) * v.opaqueJobCapacity +
                                 static_cast<std::uint32_t>(v.allBatches.size()) * CullJobsAll;
  v.commandBase = m_commandsReserved;
  v.visibleBase = m_visibleReserved;
  v.directBase = v.visibleBase + opaqueDraws * v.opaqueJobCapacity + allDraws * CullJobsAll;
  m_commandsReserved += commands;
  m_visibleReserved = v.directBase + static_cast<std::uint32_t>(v.resolved.size());
  if (commands == 0) {
    return;
  }
  const std::uint32_t visible = m_visibleReserved;
  if (m_commandsReserved > m_commandCapacity) {
    if (m_commandBuffer) {
      m_device.destroyBuffer(m_commandBuffer); // deferred past the frames still drawing from it
    }
    m_commandCapacity = m_commandsReserved;
    m_commandBuffer = m_device.createBuffer({.size = std::uint64_t{m_commandsReserved} * sizeof(rhi::IndirectCommand),
                                             .usage = rhi::BufferUsage::Storage | rhi::BufferUsage::Indirect,
                                             .debugName = "draw commands"});
  }
  if (visible > m_visibleCapacity) {
    if (m_visibleBuffer) {
      m_device.destroyBuffer(m_visibleBuffer);
    }
    m_visibleCapacity = visible;
    m_visibleBuffer = m_device.createBuffer({.size = std::uint64_t{visible} * sizeof(std::uint32_t),
                                             .usage = rhi::BufferUsage::Storage | rhi::BufferUsage::TransferDst,
                                             .debugName = "visible draws"});
  }
}

std::optional<Renderer::CullJob> Renderer::reserveCullJob(ViewState &v, bool opaque) {
  if (!m_commandBuffer || !m_visibleBuffer) {
    return std::nullopt; // nothing to draw this frame
  }
  const auto opaqueDraws = static_cast<std::uint32_t>(v.opaqueOrder.size());
  const auto allDraws = static_cast<std::uint32_t>(v.allOrder.size());
  const auto opaqueBatches = static_cast<std::uint32_t>(v.opaqueBatches.size());
  const auto allBatches = static_cast<std::uint32_t>(v.allBatches.size());
  CullJob job;
  job.opaque = opaque;
  if (opaque) {
    if (v.opaqueJobsUsed >= v.opaqueJobCapacity || opaqueDraws == 0) {
      return std::nullopt;
    }
    job.drawCount = opaqueDraws;
    job.batchCount = opaqueBatches;
    job.firstCommand = v.commandBase + v.opaqueJobsUsed * opaqueBatches;
    job.firstVisible = v.visibleBase + v.opaqueJobsUsed * opaqueDraws;
    ++v.opaqueJobsUsed;
  } else {
    if (v.allJobsUsed >= CullJobsAll || allDraws == 0) {
      return std::nullopt;
    }
    job.drawCount = allDraws;
    job.batchCount = allBatches;
    job.firstCommand = v.commandBase + v.opaqueJobCapacity * opaqueBatches + v.allJobsUsed * allBatches;
    job.firstVisible = v.visibleBase + v.opaqueJobCapacity * opaqueDraws + v.allJobsUsed * allDraws;
    ++v.allJobsUsed;
  }
  return job;
}

void Renderer::recordCulling(rhi::ICommandList &commands, ViewState &v) {
  SONNET_ZONE();
  const std::span<const CullJob> pending{v.cullJobs.begin() + static_cast<std::ptrdiff_t>(v.firstPendingJob),
                                         v.cullJobs.end()};
  v.firstPendingJob = v.cullJobs.size();
  if (pending.empty() || !v.frameBuffers.valid()) {
    return;
  }
  // The commands and the visible list this frame overwrites are the ones the previous frame's
  // draws fetched and read, and those may still be running.
  commands.memoryBarrier({.srcStage = rhi::PipelineStage::DrawIndirect | rhi::PipelineStage::VertexShader,
                          .srcAccess = rhi::Access::IndirectCommandRead | rhi::Access::ShaderRead,
                          .dstStage = rhi::PipelineStage::ComputeShader,
                          .dstAccess = rhi::Access::ShaderRead | rhi::Access::ShaderWrite});
  const rhi::BufferBinding commandWords{.binding = rhi::PassStorageBinding, .buffer = m_commandBuffer};
  const std::uint64_t visible = m_device.bufferAddress(m_visibleBuffer);
  const auto candidates = [&v](const CullJob &job) {
    return job.opaque ? v.frameBuffers.opaqueCandidates : v.frameBuffers.allCandidates;
  };
  const auto push = [&](const CullJob &job, std::uint32_t threads) {
    const CullConstants constants{.viewProjection = job.viewProjection,
                                  .draws = candidates(job),
                                  .visible = visible,
                                  .drawCount = threads,
                                  .commandBase = job.firstCommand,
                                  .visibleBase = job.firstVisible,
                                  .selectedOnly = job.selectedOnly ? 1u : 0u};
    commands.pushConstants(std::as_bytes(std::span{&constants, 1}));
  };

  // Every batch starts with a command that draws nothing; survivors add their instances.
  commands.bindPipeline(m_clearCommandsPipeline);
  commands.bindBuffers({&commandWords, 1});
  for (const CullJob &job : pending) {
    push(job, job.batchCount);
    commands.dispatch(groups(job.batchCount, CullThreads), 1, 1);
  }
  commands.memoryBarrier({.srcStage = rhi::PipelineStage::ComputeShader,
                          .srcAccess = rhi::Access::ShaderWrite,
                          .dstStage = rhi::PipelineStage::ComputeShader,
                          .dstAccess = rhi::Access::ShaderRead | rhi::Access::ShaderWrite});

  commands.bindPipeline(m_cullPipeline);
  commands.bindBuffers({&commandWords, 1});
  for (const CullJob &job : pending) {
    if (candidates(job) == 0) {
      continue; // the candidates did not fit the frame's transient memory; the commands stay empty
    }
    push(job, job.drawCount);
    commands.dispatch(groups(job.drawCount, CullThreads), 1, 1);
  }
  commands.memoryBarrier({.srcStage = rhi::PipelineStage::ComputeShader,
                          .srcAccess = rhi::Access::ShaderWrite,
                          .dstStage = rhi::PipelineStage::DrawIndirect | rhi::PipelineStage::VertexShader,
                          .dstAccess = rhi::Access::IndirectCommandRead | rhi::Access::ShaderRead});
}

void Renderer::recordIndirect(rhi::ICommandList &commands, ViewState &v, const CullJob &job,
                              std::span<const Batch> batches, std::span<const rhi::PipelineHandle, 2> pipelines,
                              std::uint32_t shadow) {
  if (batches.empty() || !v.frameBuffers.valid()) {
    return;
  }
  rhi::PipelineHandle bound;
  bool mirrored = false;
  const Mesh *boundMesh = nullptr;
  for (std::uint32_t index = 0; index < batches.size(); ++index) {
    const Batch &batch = batches[index];
    const rhi::PipelineHandle pipeline = pipelines[batch.pipeline];
    if (pipeline != bound) {
      commands.bindPipeline(pipeline); // which resets the front face to counter-clockwise
      bindFrame(commands, v);
      const DrawConstants push{shadow};
      commands.pushConstants(std::as_bytes(std::span{&push, 1}));
      bound = pipeline;
      mirrored = false;
    }
    if (batch.mirrored != mirrored) {
      commands.setFrontFace(batch.mirrored ? rhi::FrontFace::Clockwise : rhi::FrontFace::CounterClockwise);
      mirrored = batch.mirrored;
    }
    // Batches of one mesh's submeshes follow each other and share its index buffer.
    if (batch.mesh != boundMesh) {
      commands.bindIndexBuffer(batch.mesh->indices, rhi::IndexType::Uint32);
      boundMesh = batch.mesh;
    }
    commands.drawIndexedIndirect(m_commandBuffer,
                                 std::uint64_t{job.firstCommand + index} * sizeof(rhi::IndirectCommand), 1);
    ++v.statistics.indirectCallCount;
  }
}

std::uint32_t Renderer::resolveVertices(ViewState &v, const DrawItem &item, const Mesh &mesh) {
  const SceneView &view = *v.view;
  const bool skinned = item.jointCount > 0 && item.skinInstance != 0 && mesh.skin &&
                       std::size_t{item.firstJoint} + item.jointCount <= view.joints.size();
  const bool morphed = item.morphWeightCount > 0 && item.skinInstance != 0 && mesh.morph &&
                       std::size_t{item.firstMorphWeight} + item.morphWeightCount <= view.morphWeights.size();
  if (!skinned && !morphed) {
    return m_device.storageBufferIndex(mesh.vertices);
  }
  SkinnedVertices &instance = m_skinned[item.skinInstance];
  if (instance.mesh != item.mesh || !instance.buffer) {
    if (instance.buffer) {
      m_device.destroyBuffer(instance.buffer);
    }
    instance.mesh = item.mesh;
    instance.buffer = m_device.createBuffer({.size = std::uint64_t{mesh.vertexCount} * sizeof(Vertex),
                                             .usage = rhi::BufferUsage::Storage,
                                             .debugName = std::format("{} skinned", mesh.debugName)});
    instance.lastFrame = m_graphFrame + 1; // not yet scheduled this frame
  }
  // The submeshes of one instance share its vertices: deformed once.
  if (instance.lastFrame != m_graphFrame) {
    instance.lastFrame = m_graphFrame;
    v.skinJobs.push_back(SkinJob{.mesh = &mesh,
                                 .destination = instance.buffer,
                                 .firstJoint = item.firstJoint,
                                 .jointCount = skinned ? item.jointCount : 0,
                                 .firstMorphWeight = item.firstMorphWeight,
                                 .morphCount = morphed ? std::min(item.morphWeightCount, mesh.morphTargetCount) : 0});
    ++v.statistics.skinnedInstanceCount;
    v.statistics.skinnedVertexCount += mesh.vertexCount;
  }
  return m_device.storageBufferIndex(instance.buffer);
}

void Renderer::releaseSkinnedVertices(bool all) {
  for (auto it = m_skinned.begin(); it != m_skinned.end();) {
    const bool stale = it->second.lastFrame + SkinnedBufferFrames < m_graphFrame || !m_meshes.contains(it->second.mesh);
    if (all || stale) {
      m_device.destroyBuffer(it->second.buffer);
      it = m_skinned.erase(it);
    } else {
      ++it;
    }
  }
}

void Renderer::recordSkinning(rhi::ICommandList &commands, const ViewState &v) {
  SONNET_ZONE();
  if (v.view == nullptr || (v.view->joints.empty() && v.view->morphWeights.empty())) {
    return;
  }
  const std::span<const glm::mat4> joints = v.view->joints;
  const std::span<const float> morphWeights = v.view->morphWeights;
  std::uint64_t jointAddress = 0;
  std::uint64_t weightAddress = 0;
  if (!joints.empty()) {
    const rhi::TransientAllocation allocation = m_device.allocateTransient(joints.size_bytes());
    if (allocation.data.empty()) {
      return; // the allocator logged the exhaustion; the instances keep last frame's pose
    }
    std::memcpy(allocation.data.data(), joints.data(), joints.size_bytes());
    jointAddress = m_device.bufferAddress(allocation.buffer) + allocation.offset;
  }
  if (!morphWeights.empty()) {
    const rhi::TransientAllocation allocation = m_device.allocateTransient(morphWeights.size_bytes());
    if (allocation.data.empty()) {
      return;
    }
    std::memcpy(allocation.data.data(), morphWeights.data(), morphWeights.size_bytes());
    weightAddress = m_device.bufferAddress(allocation.buffer) + allocation.offset;
  }
  // The instance buffers were written by the previous frame's skinning and read by its draws,
  // which may still be running.
  commands.memoryBarrier({.srcStage = rhi::PipelineStage::VertexShader | rhi::PipelineStage::ComputeShader,
                          .srcAccess = rhi::Access::ShaderRead | rhi::Access::ShaderWrite,
                          .dstStage = rhi::PipelineStage::ComputeShader,
                          .dstAccess = rhi::Access::ShaderWrite});
  commands.bindPipeline(m_skinPipeline);
  for (const SkinJob &job : v.skinJobs) {
    const SkinConstants constants{.source = m_device.bufferAddress(job.mesh->vertices),
                                  .skin = job.jointCount > 0 ? m_device.bufferAddress(job.mesh->skin) : 0,
                                  .joints = jointAddress + std::uint64_t{job.firstJoint} * sizeof(glm::mat4),
                                  .destination = m_device.bufferAddress(job.destination),
                                  .morph = job.morphCount > 0 ? m_device.bufferAddress(job.mesh->morph) : 0,
                                  .weights = weightAddress + std::uint64_t{job.firstMorphWeight} * sizeof(float),
                                  .vertexCount = job.mesh->vertexCount,
                                  .jointCount = job.jointCount,
                                  .morphCount = job.morphCount,
                                  .padding = 0};
    commands.pushConstants(std::as_bytes(std::span{&constants, 1}));
    commands.dispatch(groups(job.mesh->vertexCount, SkinThreads), 1, 1);
  }
  commands.memoryBarrier({.srcStage = rhi::PipelineStage::ComputeShader,
                          .srcAccess = rhi::Access::ShaderWrite,
                          .dstStage = rhi::PipelineStage::VertexShader,
                          .dstAccess = rhi::Access::ShaderRead});
}

void Renderer::resetParticles(std::uint64_t key) {
  const auto found = m_particles.find(key);
  if (found == m_particles.end()) {
    return;
  }
  for (const rhi::BufferHandle buffer : {found->second.particles, found->second.alive, found->second.command}) {
    m_device.destroyBuffer(buffer);
  }
  m_particles.erase(found);
}

void Renderer::releaseParticles(bool all) {
  for (auto it = m_particles.begin(); it != m_particles.end();) {
    if (all || it->second.lastFrame + ParticleStateFrames < m_graphFrame) {
      for (const rhi::BufferHandle buffer : {it->second.particles, it->second.alive, it->second.command}) {
        m_device.destroyBuffer(buffer);
      }
      it = m_particles.erase(it);
    } else {
      ++it;
    }
  }
}

void Renderer::addParticlePass(RenderGraph &graph, ViewState &v) {
  const SceneView &view = *v.view;
  const float dt = std::clamp(view.deltaTime, 0.0f, MaxParticleStep);
  bool simulating = false;
  for (const ParticleEmitterItem &item : view.particles) {
    if (item.key == 0 || item.maxParticles == 0) {
      continue;
    }
    const std::uint32_t capacity = std::min(item.maxParticles, MaxParticlesPerEmitter);
    ParticleState &state = m_particles[item.key];
    if (state.maxParticles != capacity) {
      // New, or resized: a fresh ring of dead particles (the buffer is zeroed, and age 0 of
      // lifetime 0 is dead).
      for (const rhi::BufferHandle buffer : {state.particles, state.alive, state.command}) {
        if (buffer) {
          m_device.destroyBuffer(buffer);
        }
      }
      state = {};
      state.maxParticles = capacity;
      state.particles = m_device.createBuffer({.size = std::uint64_t{capacity} * 32,
                                               .usage = rhi::BufferUsage::Storage | rhi::BufferUsage::TransferDst,
                                               .debugName = "particles"});
      const std::vector<std::byte> zeros(std::size_t{capacity} * 32, std::byte{0});
      m_device.uploadBuffer(state.particles, 0, zeros);
      state.alive = m_device.createBuffer({.size = std::uint64_t{capacity} * sizeof(std::uint32_t),
                                           .usage = rhi::BufferUsage::Storage,
                                           .debugName = "particles alive"});
      state.command = m_device.createBuffer({.size = sizeof(rhi::IndirectCommand),
                                             .usage = rhi::BufferUsage::Storage | rhi::BufferUsage::Indirect,
                                             .debugName = "particles command"});
    }
    state.lastFrame = m_graphFrame;
    ParticleJob job{.state = &state,
                    .item = &item,
                    .simulate = false,
                    .spawnCount = 0,
                    .spawnHead = state.head,
                    .serial = state.serial,
                    .dt = 0.0f,
                    .distance = glm::length(glm::vec3{item.transform[3]} - view.camera.position)};
    // The first view to see an emitter in a graph frame advances it; a second view only draws.
    if (state.simulatedFrame != m_graphFrame) {
      state.simulatedFrame = m_graphFrame;
      job.simulate = true;
      simulating = true;
      if (item.simulate) {
        job.dt = dt;
        state.carry += item.rate * dt;
        std::uint32_t born = static_cast<std::uint32_t>(std::floor(state.carry));
        state.carry -= static_cast<float>(born);
        if (!state.burstDone) {
          born += item.burst;
          state.burstDone = true;
        }
        job.spawnCount = std::min(born, capacity);
        state.head = (state.head + job.spawnCount) % capacity;
        state.serial += job.spawnCount;
      }
    }
    v.particleJobs.push_back(job);
    v.statistics.particleSlotCount += capacity;
  }
  v.statistics.particleEmitterCount = static_cast<std::uint32_t>(v.particleJobs.size());
  // Far to near, so nearer emitters blend over farther ones.
  std::ranges::stable_sort(v.particleJobs, std::greater<>{}, &ParticleJob::distance);
  if (simulating) {
    graph.addPass(
        "particles", [](PassBuilder &) {},
        [this, &v](rhi::ICommandList &commands, const PassResources &) { recordParticleSimulation(commands, v); });
  }
}

void Renderer::recordParticleSimulation(rhi::ICommandList &commands, ViewState &v) {
  SONNET_ZONE();
  // Last frame's draws read what this frame's simulation rewrites, and may still be running.
  commands.memoryBarrier({.srcStage = rhi::PipelineStage::DrawIndirect | rhi::PipelineStage::VertexShader,
                          .srcAccess = rhi::Access::IndirectCommandRead | rhi::Access::ShaderRead,
                          .dstStage = rhi::PipelineStage::ComputeShader,
                          .dstAccess = rhi::Access::ShaderRead | rhi::Access::ShaderWrite});
  const std::uint32_t sampler = m_device.samplerIndex(m_linearClampSampler);
  for (ParticleJob &job : v.particleJobs) {
    if (!job.simulate) {
      continue;
    }
    const ParticleEmitterItem &item = *job.item;
    const rhi::TransientAllocation allocation = m_device.allocateTransient(sizeof(ParticleParams));
    if (allocation.data.empty()) {
      job.simulate = false; // the allocator logged the exhaustion; the emitter keeps last frame's particles
      continue;
    }
    const ParticleParams params{.model = item.transform,
                                .colorStart = item.colorStart,
                                .colorEnd = item.colorEnd,
                                .gravity = item.gravity,
                                .drag = item.drag,
                                .lifetime = item.lifetime,
                                .speed = item.speed,
                                .coneAngle = item.coneAngle,
                                .sizeStart = item.sizeStart,
                                .sizeEnd = item.sizeEnd,
                                .dt = job.dt,
                                .maxParticles = job.state->maxParticles,
                                .spawnCount = job.spawnCount,
                                .spawnHead = job.spawnHead,
                                .serial = job.serial,
                                .seed = item.seed,
                                .texture = item.texture ? textureIndex(item.texture) : rhi::InvalidBindlessIndex,
                                .flags = (item.local ? ParticleFlagLocal : 0u) |
                                         (item.blend == ParticleBlend::Additive ? ParticleFlagAdditive : 0u),
                                .sampler = sampler};
    std::memcpy(allocation.data.data(), &params, sizeof(params));
    job.state->params = m_device.bufferAddress(allocation.buffer) + allocation.offset;
  }

  // Every command starts the frame empty.
  commands.bindPipeline(m_particleResetPipeline);
  for (const ParticleJob &job : v.particleJobs) {
    if (job.simulate) {
      const rhi::BufferBinding command{.binding = rhi::PassStorageBinding, .buffer = job.state->command};
      commands.bindBuffers({&command, 1});
      commands.dispatch(1, 1, 1);
    }
  }
  commands.memoryBarrier({.srcStage = rhi::PipelineStage::ComputeShader,
                          .srcAccess = rhi::Access::ShaderWrite,
                          .dstStage = rhi::PipelineStage::ComputeShader,
                          .dstAccess = rhi::Access::ShaderRead | rhi::Access::ShaderWrite});

  commands.bindPipeline(m_particleSimulatePipeline);
  for (const ParticleJob &job : v.particleJobs) {
    if (!job.simulate) {
      continue;
    }
    const rhi::BufferBinding command{.binding = rhi::PassStorageBinding, .buffer = job.state->command};
    commands.bindBuffers({&command, 1});
    const ParticleSimulateConstants constants{.particles = m_device.bufferAddress(job.state->particles),
                                              .alive = m_device.bufferAddress(job.state->alive),
                                              .params = job.state->params};
    commands.pushConstants(std::as_bytes(std::span{&constants, 1}));
    commands.dispatch(groups(job.state->maxParticles, ParticleThreads), 1, 1);
  }
  commands.memoryBarrier({.srcStage = rhi::PipelineStage::ComputeShader,
                          .srcAccess = rhi::Access::ShaderWrite,
                          .dstStage = rhi::PipelineStage::DrawIndirect | rhi::PipelineStage::VertexShader,
                          .dstAccess = rhi::Access::IndirectCommandRead | rhi::Access::ShaderRead});
}

void Renderer::recordParticleDraws(rhi::ICommandList &commands, const ViewState &v) {
  if (v.particleJobs.empty()) {
    return;
  }
  const SceneView &view = *v.view;
  const float aspect = static_cast<float>(v.targetSize.x) / static_cast<float>(std::max(v.targetSize.y, 1u));
  const glm::mat4 viewProjection = view.camera.projection(aspect) * view.camera.view();
  bool boundQuad = false;
  for (const ParticleJob &job : v.particleJobs) {
    if (job.state->params == 0) {
      continue; // never simulated: nothing to draw yet
    }
    commands.bindPipeline(m_particleDrawPipelines[job.item->blend == ParticleBlend::Additive ? 1 : 0]);
    const ParticleDrawConstants constants{.viewProjection = viewProjection,
                                          .cameraRight = view.camera.right(),
                                          .cameraUp = view.camera.up(),
                                          .particles = m_device.bufferAddress(job.state->particles),
                                          .alive = m_device.bufferAddress(job.state->alive),
                                          .params = job.state->params};
    commands.pushConstants(std::as_bytes(std::span{&constants, 1}));
    if (!boundQuad) {
      commands.bindIndexBuffer(m_particleQuad, rhi::IndexType::Uint32);
      boundQuad = true;
    }
    commands.drawIndexedIndirect(job.state->command, 0, 1);
  }
}

void Renderer::ensureFrameUploaded(ViewState &v, const PassResources &resources) {
  if (v.frameBuffers.uploaded || v.view == nullptr) {
    return;
  }
  v.frameBuffers.uploaded = true;
  const SceneView &view = *v.view;
  const std::uint32_t lightCount = static_cast<std::uint32_t>(std::min<std::size_t>(view.lights.size(), MaxLights));
  const std::uint32_t materialCount = m_materialSlots + 1;
  v.frameBuffers.frame = m_device.allocateTransient(sizeof(FrameConstants));
  v.frameBuffers.objects = m_device.allocateTransient(std::max<std::size_t>(view.draws.size(), 1) * sizeof(ObjectData));
  const rhi::TransientAllocation materials = m_device.allocateTransient(materialCount * sizeof(GpuMaterial));
  const rhi::TransientAllocation lights = m_device.allocateTransient(std::max(lightCount, 1u) * sizeof(GpuLight));
  if (!v.frameBuffers.valid() || materials.data.empty() || lights.data.empty()) {
    v.frameBuffers = {}; // the allocator logged the exhaustion; the passes draw nothing
    v.frameBuffers.uploaded = true;
    return;
  }

  // Objects, in the draw list's order so the object index is the draw index. A draw whose mesh
  // handle went stale keeps a null vertex address and is never in an order list.
  auto *objects = reinterpret_cast<ObjectData *>(v.frameBuffers.objects.data.data());
  // One slot per draw, written once, read by nobody until the pass records: the loop the job
  // system exists for (ADR-0013). What it costs is the bytes, not the arithmetic.
  parallelFor("frame objects", view.draws.size(), ObjectGrain, [&](std::size_t begin, std::size_t end) {
    for (std::size_t i = begin; i < end; ++i) {
      const DrawItem &item = view.draws[i];
      objects[i] = ObjectData{.model = item.transform,
                              .color = item.color,
                              .id = item.id,
                              .material = materialIndex(item.material),
                              .vertexBuffer = 0};
    }
  });
  for (const ResolvedDraw &draw : v.resolved) {
    objects[draw.objectIndex].vertexBuffer = draw.vertexBuffer;
  }

  // The culling pass's candidates, one array per order list, each grouped into its batches.
  const auto uploadCandidates = [&](std::span<const std::uint32_t> order, std::span<const Batch> batches,
                                    std::uint64_t &address) {
    address = 0;
    if (order.empty()) {
      return;
    }
    const rhi::TransientAllocation allocation = m_device.allocateTransient(order.size() * sizeof(CullDraw));
    if (allocation.data.empty()) {
      return; // the allocator logged the exhaustion; the passes fall back to the direct path
    }
    auto *candidates = reinterpret_cast<CullDraw *>(allocation.data.data());
    // Split over the batches rather than the draws: a batch is a contiguous run of positions, so
    // every job still writes a range of its own, and a batch knows its own index.
    parallelFor("cull candidates", batches.size(), BatchGrain, [&](std::size_t first, std::size_t last) {
      for (std::size_t index = first; index < last; ++index) {
        const Batch &batch = batches[index];
        for (std::uint32_t offset = 0; offset < batch.drawCount; ++offset) {
          const std::uint32_t position = batch.firstDraw + offset;
          const ResolvedDraw &draw = v.resolved[order[position]];
          const bool selected =
              !m_selected.empty() && std::ranges::binary_search(m_selected, view.draws[draw.objectIndex].id);
          candidates[position] = CullDraw{.center = draw.center,
                                          .extent = draw.extent,
                                          .objectIndex = draw.objectIndex,
                                          .indexCount = draw.submesh.indexCount,
                                          .firstIndex = draw.submesh.firstIndex,
                                          .batch = static_cast<std::uint32_t>(index),
                                          .batchFirst = batch.firstDraw,
                                          .flags = selected ? CullSelected : 0u};
        }
      }
    });
    address = m_device.bufferAddress(allocation.buffer) + allocation.offset;
  };
  uploadCandidates(v.opaqueOrder, v.opaqueBatches, v.frameBuffers.opaqueCandidates);
  uploadCandidates(v.allOrder, v.allBatches, v.frameBuffers.allCandidates);

  // Materials: slot 0 the default, then every live material by pool index.
  auto *gpuMaterials = reinterpret_cast<GpuMaterial *>(materials.data.data());
  const auto encode = [this](const MaterialDesc &desc) {
    const std::uint32_t mode = desc.alphaMode == AlphaMode::Blend  ? AlphaBlend
                               : desc.alphaMode == AlphaMode::Mask ? AlphaMask
                                                                   : AlphaOpaque;
    const Texture *normal = m_textures.find(desc.normalTexture);
    return GpuMaterial{.baseColor = desc.baseColor,
                       .emissive = glm::vec4{desc.emissive, desc.alphaCutoff},
                       .metallic = desc.metallic,
                       .roughness = desc.roughness,
                       .normalScale = desc.normalScale,
                       .occlusionStrength = desc.occlusionStrength,
                       .baseColorTexture = textureIndex(desc.baseColorTexture),
                       .metallicRoughnessTexture = textureIndex(desc.metallicRoughnessTexture),
                       .normalTexture = normal != nullptr ? m_device.sampledImageIndex(normal->image)
                                                          : textureIndex(m_flatNormalTexture),
                       .occlusionTexture = textureIndex(desc.occlusionTexture),
                       .emissiveTexture = textureIndex(desc.emissiveTexture),
                       .flags = mode | (desc.doubleSided ? MaterialDoubleSided : 0u),
                       .sampler = m_device.samplerIndex(m_materialSamplers[static_cast<std::size_t>(desc.wrap)]),
                       .padding = 0};
  };
  MaterialDesc defaultMaterial;
  defaultMaterial.metallic = 0.0f;
  defaultMaterial.roughness = 0.5f;
  for (std::uint32_t i = 0; i < materialCount; ++i) {
    gpuMaterials[i] = encode(defaultMaterial);
  }
  m_materials.forEach(
      [&](MaterialHandle handle, const Material &material) { gpuMaterials[handle.index + 1] = encode(material.desc); });

  // The budgeted lights' maps, matrices and depth slots, and which light each belongs to.
  rhi::TransientAllocation localShadows;
  if (!v.localShadows.empty()) {
    localShadows = m_device.allocateTransient(v.localShadows.size() * sizeof(GpuLocalShadow));
    if (!localShadows.data.empty()) {
      auto *gpuShadows = reinterpret_cast<GpuLocalShadow *>(localShadows.data.data());
      for (std::size_t k = 0; k < v.localShadows.size(); ++k) {
        gpuShadows[k] = GpuLocalShadow{.matrix = v.localShadows[k].matrix,
                                       .image = sampledIndex(resources.image(v.localShadows[k].image)),
                                       .padding = {}};
      }
    }
  }
  std::vector<std::uint32_t> lightShadow(lightCount, NoLocalShadow);
  if (!localShadows.data.empty()) {
    // A point light's index names its first face; the others follow it.
    for (std::size_t k = v.localShadows.size(); k-- > 0;) {
      lightShadow[v.localShadows[k].light] = static_cast<std::uint32_t>(k);
    }
  }
  auto *gpuLights = reinterpret_cast<GpuLight *>(lights.data.data());
  for (std::uint32_t i = 0; i < lightCount; ++i) {
    const Light &light = view.lights[i];
    gpuLights[i] = GpuLight{.position = light.position,
                            .range = std::max(light.range, 1.0e-3f),
                            .color = light.color * light.intensity,
                            .type = light.type == LightType::Spot ? LightSpot : LightPoint,
                            .direction = glm::normalize(light.direction),
                            .innerCos = std::cos(light.innerAngle),
                            .outerCos = std::cos(light.outerAngle),
                            .shadow = lightShadow[i],
                            .padding = {}};
  }
  v.statistics.lightCount = lightCount;

  const float aspect = static_cast<float>(v.targetSize.x) / static_cast<float>(std::max(v.targetSize.y, 1u));
  const glm::mat4 cameraView = view.camera.view();
  const glm::mat4 projection = view.camera.projection(aspect);
  const float nearPlane = view.camera.nearPlane;
  const float sliceScale = static_cast<float>(ClusterGridZ) / std::log(ClusterFar / nearPlane);
  const Environment *environment = m_environments.find(view.environment);
  const bool environmentReady = environment != nullptr && !environment->pending;
  FrameConstants frame{
      .view = cameraView,
      .projection = projection,
      .viewProjection = projection * cameraView,
      .inverseProjection = glm::inverse(projection),
      .inverseViewProjection = glm::inverse(projection * cameraView),
      .cascadeMatrices = {},
      .cascadeSplits = {},
      .cascadeBlendStarts = {},
      .cascadeTexelSizes = {},
      .cameraPosition = glm::vec4{view.camera.position, 1.0f},
      .sunDirection = glm::vec4{glm::normalize(view.sun.direction), view.hasSun ? 1.0f : 0.0f},
      .sunColor = glm::vec4{view.sun.color * view.sun.intensity, 0.0f},
      .ambient = glm::vec4{view.ambient, 0.0f},
      .environment = glm::vec4{view.environmentIntensity, static_cast<float>(m_settings.prefilteredLevels),
                               view.exposure, environmentReady ? 1.0f : 0.0f},
      .clusterScaleBias =
          glm::vec4{static_cast<float>(v.targetSize.x) / ClusterGridX,
                    static_cast<float>(v.targetSize.y) / ClusterGridY, sliceScale, -sliceScale * std::log(nearPlane)},
      .targetSize = glm::vec2{v.targetSize},
      .nearPlane = nearPlane,
      .shadowBias = m_settings.shadowBias,
      .cascadeImages = {},
      .irradianceCube = environmentReady ? sampledIndex(environment->irradiance) : rhi::InvalidBindlessIndex,
      .prefilteredCube = environmentReady ? sampledIndex(environment->prefiltered) : rhi::InvalidBindlessIndex,
      .skyboxCube = environmentReady ? sampledIndex(environment->skybox) : rhi::InvalidBindlessIndex,
      .brdfLut = sampledIndex(m_brdfLut),
      .linearSampler = m_device.samplerIndex(m_linearClampSampler),
      .shadowSampler = m_device.samplerIndex(m_shadowSampler),
      .lightCount = lightCount,
      .debugView = static_cast<std::uint32_t>(m_settings.debugView),
      .visible = m_visibleBuffer ? m_device.bufferAddress(m_visibleBuffer) : 0,
      .materials = m_device.bufferAddress(materials.buffer) + materials.offset,
      .lights = m_device.bufferAddress(lights.buffer) + lights.offset,
      .clusters = m_device.bufferAddress(m_clusterBuffer),
      .localShadows = localShadows.data.empty() ? 0 : m_device.bufferAddress(localShadows.buffer) + localShadows.offset,
  };
  for (std::uint32_t c = 0; c < CascadeCount; ++c) {
    frame.cascadeMatrices[c] = v.cascades[c].matrix;
    frame.cascadeSplits[static_cast<int>(c)] = v.cascades[c].split;
    frame.cascadeBlendStarts[static_cast<int>(c)] = v.cascades[c].blendStart;
    frame.cascadeTexelSizes[static_cast<int>(c)] = v.cascades[c].texelSize;
    frame.cascadeImages[c] =
        v.cascadesActive ? sampledIndex(resources.image(v.cascadeImages[c])) : rhi::InvalidBindlessIndex;
  }
  std::memcpy(v.frameBuffers.frame.data.data(), &frame, sizeof(frame));
}

void Renderer::bindFrame(rhi::ICommandList &commands, const ViewState &v) {
  const std::array bindings{
      rhi::BufferBinding{.binding = rhi::PassUniformBinding,
                         .buffer = v.frameBuffers.frame.buffer,
                         .offset = v.frameBuffers.frame.offset,
                         .size = v.frameBuffers.frame.data.size()},
      rhi::BufferBinding{.binding = rhi::PassStorageBinding,
                         .buffer = v.frameBuffers.objects.buffer,
                         .offset = v.frameBuffers.objects.offset,
                         .size = v.frameBuffers.objects.data.size()},
  };
  commands.bindBuffers(bindings);
}

void Renderer::recordDraws(rhi::ICommandList &commands, ViewState &v, std::span<const std::uint32_t> order,
                           std::span<const rhi::PipelineHandle, 2> pipelines, bool count, std::uint32_t shadow) {
  if (order.empty() || !v.frameBuffers.valid() || !m_visibleBuffer) {
    return;
  }
  // The direct range names each resolved draw's object, written once a frame through the staging
  // ring, which lands before this frame's commands (ADR-0016).
  if (!v.frameBuffers.directSlotsUploaded) {
    v.frameBuffers.directSlotsUploaded = true;
    v.directSlots.resize(v.resolved.size());
    for (std::size_t i = 0; i < v.resolved.size(); ++i) {
      v.directSlots[i] = v.resolved[i].objectIndex;
    }
    m_device.uploadBuffer(m_visibleBuffer, std::uint64_t{v.directBase} * sizeof(std::uint32_t),
                          std::as_bytes(std::span{v.directSlots}));
  }
  rhi::PipelineHandle bound;
  bool mirrored = false;
  const Mesh *boundMesh = nullptr;
  for (const std::uint32_t index : order) {
    const ResolvedDraw &draw = v.resolved[index];
    const rhi::PipelineHandle pipeline = pipelines[draw.doubleSided ? 1 : 0];
    if (pipeline != bound) {
      commands.bindPipeline(pipeline); // which resets the front face to counter-clockwise
      bindFrame(commands, v);
      // The shadow map is the whole of the per-draw constants now; the draw's slot in the visible
      // list rides in its first instance, as a batch's does in an indirect command (ADR-0016).
      const DrawConstants push{shadow};
      commands.pushConstants(std::as_bytes(std::span{&push, 1}));
      bound = pipeline;
      mirrored = false;
    }
    if (draw.mirrored != mirrored) {
      commands.setFrontFace(draw.mirrored ? rhi::FrontFace::Clockwise : rhi::FrontFace::CounterClockwise);
      mirrored = draw.mirrored;
    }
    if (draw.mesh != boundMesh) {
      commands.bindIndexBuffer(draw.mesh->indices, rhi::IndexType::Uint32);
      boundMesh = draw.mesh;
    }
    commands.drawIndexed(draw.submesh.indexCount, 1, draw.submesh.firstIndex, 0, v.directBase + index);
    if (count) {
      ++v.statistics.drawCount;
      v.statistics.triangleCount += draw.submesh.indexCount / 3;
    }
  }
}

void Renderer::recordClustering(rhi::ICommandList &commands, const ViewState &v) {
  if (!v.frameBuffers.valid()) {
    return;
  }
  // The previous frame's fragments may still read the clusters this frame overwrites.
  commands.memoryBarrier({.srcStage = rhi::PipelineStage::FragmentShader,
                          .srcAccess = rhi::Access::ShaderRead,
                          .dstStage = rhi::PipelineStage::ComputeShader,
                          .dstAccess = rhi::Access::ShaderWrite});
  commands.bindPipeline(m_clusterPipeline);
  bindFrame(commands, v);
  commands.dispatch(groups(ClusterGridX, 4), groups(ClusterGridY, 3), groups(ClusterGridZ, 4));
  commands.memoryBarrier({.srcStage = rhi::PipelineStage::ComputeShader,
                          .srcAccess = rhi::Access::ShaderWrite,
                          .dstStage = rhi::PipelineStage::FragmentShader,
                          .dstAccess = rhi::Access::ShaderRead});
}

void Renderer::recordPost(rhi::ICommandList &commands, const SceneView *view, rhi::PipelineHandle pipeline,
                          rhi::ImageHandle source, rhi::ImageHandle secondary, glm::uvec2 targetSize) {
  const glm::uvec2 sourceSize = m_device.imageDesc(source).size;
  const PostConstants push{.source = sampledIndex(source),
                           .secondary = secondary ? sampledIndex(secondary) : rhi::InvalidBindlessIndex,
                           .sampler = m_device.samplerIndex(m_linearClampSampler),
                           .sourceTexel = 1.0f / glm::vec2{sourceSize},
                           .targetSize = glm::vec2{targetSize},
                           .exposure = view != nullptr ? view->exposure : 1.0f,
                           .bloomStrength = view != nullptr ? view->bloomStrength : 0.0f};
  commands.bindPipeline(pipeline);
  commands.pushConstants(std::as_bytes(std::span{&push, 1}));
  commands.draw(3);
}

void Renderer::addBloomPasses(RenderGraph &graph, const SceneView &view, GraphImage hdr, glm::uvec2 size,
                              GraphImage &result) {
  const std::uint32_t levels =
      std::min(m_settings.bloomLevels, rhi::fullMipCount(size) > 1 ? rhi::fullMipCount(size) - 1 : 1u);
  std::vector<GraphImage> down(levels);
  std::vector<GraphImage> up(levels);
  std::vector<glm::uvec2> sizes(levels);
  for (std::uint32_t i = 0; i < levels; ++i) {
    sizes[i] = rhi::mipSize(size, i + 1);
    down[i] = graph.createImage({.size = sizes[i], .format = HdrFormat, .debugName = std::format("bloom down {}", i)});
  }
  for (std::uint32_t i = 0; i < levels; ++i) {
    const GraphImage source = i == 0 ? hdr : down[i - 1];
    graph.addPass(
        std::format("bloom down {}", i),
        [&](PassBuilder &builder) {
          builder.sample(source);
          builder.color(down[i], rhi::LoadOp::DontCare);
        },
        [this, &view, source, target = sizes[i]](rhi::ICommandList &commands, const PassResources &resources) {
          recordPost(commands, &view, m_bloomDownPipeline, resources.image(source), {}, target);
        });
  }
  up[levels - 1] = down[levels - 1];
  for (std::uint32_t level = levels - 1; level-- > 0;) {
    up[level] =
        graph.createImage({.size = sizes[level], .format = HdrFormat, .debugName = std::format("bloom up {}", level)});
    graph.addPass(
        std::format("bloom up {}", level),
        [&](PassBuilder &builder) {
          builder.sample(down[level]);
          builder.sample(up[level + 1]);
          builder.color(up[level], rhi::LoadOp::DontCare);
        },
        [this, &view, source = down[level], below = up[level + 1],
         target = sizes[level]](rhi::ICommandList &commands, const PassResources &resources) {
          recordPost(commands, &view, m_bloomUpPipeline, resources.image(source), resources.image(below), target);
        });
  }
  result = up[0];
}

void Renderer::addScenePasses(RenderGraph &graph, const SceneView &view, GraphImage color, GraphImage depth,
                              glm::vec4 clearColor) {
  SONNET_ZONE();
  addPrecomputePasses(graph, view);
  const glm::uvec2 size = graph.imageDesc(color).size;
  ViewState &v = prepareFrame(graph, view, size);

  // The cascades exist only with the scene passes: the id and mask passes on their own have no
  // shadow images to sample.
  v.cascadesActive = m_settings.shadows && view.hasSun && !v.opaqueOrder.empty();
  const float aspect = static_cast<float>(size.x) / static_cast<float>(std::max(size.y, 1u));
  if (v.cascadesActive) {
    computeCascades(v, aspect);
  }
  // Every opaque job of this frame is reserved before the culling pass records, so one clear
  // and one pair of barriers covers them all (ADR-0012).
  std::array<std::optional<CullJob>, CascadeCount> cascadeJobs;
  for (std::uint32_t c = 0; c < CascadeCount && v.cascadesActive; ++c) {
    cascadeJobs[c] = reserveCullJob(v, true);
    if (cascadeJobs[c]) {
      cascadeJobs[c]->viewProjection = v.cascades[c].matrix;
    }
  }
  const glm::mat4 cameraViewProjection = view.camera.projection(aspect) * view.camera.view();
  std::optional<CullJob> depthJob = reserveCullJob(v, true);
  std::optional<CullJob> forwardJob = reserveCullJob(v, true);
  for (std::optional<CullJob> *job : {&depthJob, &forwardJob}) {
    if (*job) {
      (*job)->viewProjection = cameraViewProjection;
    }
  }
  // The lights' shadow maps, within the budget, each culled against its own frustum.
  if (m_settings.shadows && !v.opaqueOrder.empty()) {
    selectLocalShadows(v);
  }
  v.statistics.localShadowMapCount = static_cast<std::uint32_t>(v.localShadows.size());
  for (std::size_t k = 0; k < v.localShadows.size(); ++k) {
    v.statistics.localShadowCount += v.localShadows[k].face == 0 ? 1u : 0u;
  }
  std::vector<std::optional<CullJob>> localJobs(v.localShadows.size());
  for (std::size_t k = 0; k < v.localShadows.size(); ++k) {
    localJobs[k] = reserveCullJob(v, true);
    if (localJobs[k]) {
      localJobs[k]->viewProjection = v.localShadows[k].matrix;
    }
  }
  for (const std::optional<CullJob> &job : cascadeJobs) {
    if (job) {
      v.cullJobs.push_back(*job);
    }
  }
  for (const std::optional<CullJob> *job : {&depthJob, &forwardJob}) {
    if (*job) {
      v.cullJobs.push_back(**job);
    }
  }
  for (const std::optional<CullJob> &job : localJobs) {
    if (job) {
      v.cullJobs.push_back(*job);
    }
  }
  if (v.firstPendingJob < v.cullJobs.size()) {
    graph.addPass(
        "cull", [](PassBuilder &) {},
        [this, &v](rhi::ICommandList &commands, const PassResources &resources) {
          ensureFrameUploaded(v, resources);
          recordCulling(commands, v);
        });
  }

  if (v.cascadesActive) {
    for (std::uint32_t c = 0; c < CascadeCount; ++c) {
      v.cascadeImages[c] = graph.createImage({.size = {m_settings.shadowMapSize, m_settings.shadowMapSize},
                                              .format = DepthFormat,
                                              .debugName = std::format("shadow cascade {}", c)});
      graph.addPass(
          std::format("shadow cascade {}", c),
          [&](PassBuilder &builder) { builder.depth(v.cascadeImages[c], rhi::LoadOp::Clear, 0.0f); },
          [this, &v, c, job = cascadeJobs[c]](rhi::ICommandList &commands, const PassResources &resources) {
            ensureFrameUploaded(v, resources);
            if (!job) {
              recordDraws(commands, v, v.opaqueOrder, m_shadowPipelines, false, c);
              return;
            }
            recordIndirect(commands, v, *job, v.opaqueBatches, m_shadowPipelines, c);
            v.statistics.shadowDrawCount += static_cast<std::uint32_t>(v.opaqueOrder.size());
          });
    }
  }

  for (std::uint32_t k = 0; k < v.localShadows.size(); ++k) {
    const LocalShadow &map = v.localShadows[k];
    const bool point = view.lights[map.light].type == LightType::Point;
    const std::string name =
        point ? std::format("shadow point {} face {}", map.light, map.face) : std::format("shadow spot {}", map.light);
    v.localShadows[k].image = graph.createImage({.size = {m_settings.localShadowMapSize, m_settings.localShadowMapSize},
                                                 .format = DepthFormat,
                                                 .debugName = name});
    graph.addPass(
        name, [&](PassBuilder &builder) { builder.depth(v.localShadows[k].image, rhi::LoadOp::Clear, 0.0f); },
        [this, &v, k, job = localJobs[k]](rhi::ICommandList &commands, const PassResources &resources) {
          ensureFrameUploaded(v, resources);
          const std::uint32_t shadow = CascadeCount + k;
          if (!job) {
            recordDraws(commands, v, v.opaqueOrder, m_shadowPipelines, false, shadow);
            return;
          }
          recordIndirect(commands, v, *job, v.opaqueBatches, m_shadowPipelines, shadow);
          v.statistics.shadowDrawCount += static_cast<std::uint32_t>(v.opaqueOrder.size());
        });
  }

  graph.addPass(
      "depth", [&](PassBuilder &builder) { builder.depth(depth, rhi::LoadOp::Clear, 0.0f); },
      [this, &v, job = depthJob](rhi::ICommandList &commands, const PassResources &resources) {
        ensureFrameUploaded(v, resources);
        if (job) {
          recordIndirect(commands, v, *job, v.opaqueBatches, m_depthPipelines);
        } else {
          recordDraws(commands, v, v.opaqueOrder, m_depthPipelines, false);
        }
      });
  graph.addPass(
      "light clustering", [](PassBuilder &) {},
      [this, &v](rhi::ICommandList &commands, const PassResources &resources) {
        ensureFrameUploaded(v, resources);
        recordClustering(commands, v);
      });
  addParticlePass(graph, v);

  const GraphImage hdr = graph.createImage({.size = size, .format = HdrFormat, .debugName = "scene hdr"});
  const bool skybox = m_environments.contains(view.environment) && !m_environments.get(view.environment).pending;
  graph.addPass(
      "forward",
      [&](PassBuilder &builder) {
        builder.color(hdr, rhi::LoadOp::Clear, clearColor);
        builder.depth(depth, rhi::LoadOp::Load);
        if (v.cascadesActive) {
          for (const GraphImage cascade : v.cascadeImages) {
            builder.sample(cascade);
          }
        }
        for (const LocalShadow &local : v.localShadows) {
          builder.sample(local.image);
        }
        builder.sample(m_frameImages.lut);
        if (skybox) {
          builder.sample(m_frameImages.skybox);
          builder.sample(m_frameImages.irradiance);
          builder.sample(m_frameImages.prefiltered);
        }
      },
      [this, &v, skybox, job = forwardJob](rhi::ICommandList &commands, const PassResources &resources) {
        ensureFrameUploaded(v, resources);
        if (job) {
          recordIndirect(commands, v, *job, v.opaqueBatches, m_forwardPipelines);
          for (const std::uint32_t index : v.opaqueOrder) {
            ++v.statistics.drawCount;
            v.statistics.triangleCount += v.resolved[index].submesh.indexCount / 3;
          }
        } else {
          recordDraws(commands, v, v.opaqueOrder, m_forwardPipelines, true);
        }
        if (skybox && v.frameBuffers.valid()) {
          commands.bindPipeline(m_skyboxPipeline);
          bindFrame(commands, v);
          commands.draw(3);
        }
        // Blended draws keep the direct path: their order is view-dependent (ADR-0012).
        recordDraws(commands, v, v.blendedOrder, m_blendPipelines, true);
        recordParticleDraws(commands, v);
      });

  GraphImage bloom;
  if (m_settings.bloom && size.x >= 2 && size.y >= 2) {
    addBloomPasses(graph, view, hdr, size, bloom);
  }
  const GraphImage ldr = m_settings.antialiasing
                             ? graph.createImage({.size = size, .format = ColorFormat, .debugName = "scene ldr"})
                             : color;
  graph.addPass(
      "tonemap",
      [&](PassBuilder &builder) {
        builder.sample(hdr);
        if (bloom.isValid()) {
          builder.sample(bloom);
        }
        builder.color(ldr, rhi::LoadOp::DontCare);
      },
      [this, &view, hdr, bloom, size](rhi::ICommandList &commands, const PassResources &resources) {
        recordPost(commands, &view, m_tonemapPipeline, resources.image(hdr),
                   bloom.isValid() ? resources.image(bloom) : rhi::ImageHandle{}, size);
      });
  if (m_settings.antialiasing) {
    graph.addPass(
        "fxaa",
        [&](PassBuilder &builder) {
          builder.sample(ldr);
          builder.color(color, rhi::LoadOp::DontCare);
        },
        [this, &view, ldr, size](rhi::ICommandList &commands, const PassResources &resources) {
          recordPost(commands, &view, m_fxaaPipeline, resources.image(ldr), {}, size);
        });
  }
}

void Renderer::addPresentPass(RenderGraph &graph, GraphImage source, GraphImage target) {
  if (!m_presentPipeline) {
    return;
  }
  const glm::uvec2 size = graph.imageDesc(target).size;
  graph.addPass(
      "present",
      [&](PassBuilder &builder) {
        builder.sample(source);
        builder.color(target, rhi::LoadOp::DontCare);
      },
      [this, source, size](rhi::ICommandList &commands, const PassResources &resources) {
        recordPost(commands, nullptr, m_presentPipeline, resources.image(source), {}, size);
      });
}

void Renderer::addIdPass(RenderGraph &graph, const SceneView &view, GraphImage ids, GraphImage depth) {
  const glm::uvec2 size = graph.imageDesc(ids).size;
  ViewState &v = prepareFrame(graph, view, size);
  std::optional<CullJob> job = addCullPass(graph, v, false);
  graph.addPass(
      "id",
      [&](PassBuilder &builder) {
        builder.color(ids, rhi::LoadOp::Clear, {0.0f, 0.0f, 0.0f, 0.0f});
        builder.depth(depth, rhi::LoadOp::Load, 0.0f, rhi::StoreOp::DontCare);
      },
      [this, &v, job](rhi::ICommandList &commands, const PassResources &resources) {
        ensureFrameUploaded(v, resources);
        // Editor-only work; the statistics keep counting the scene, not the picking pass.
        const RenderStatistics before = v.statistics;
        if (job) {
          recordIndirect(commands, v, *job, v.allBatches, m_idPipelines);
        } else {
          recordDraws(commands, v, v.allOrder, m_idPipelines, false);
        }
        v.statistics.indirectCallCount = before.indirectCallCount;
      });
}

void Renderer::addSelectionMaskPass(RenderGraph &graph, const SceneView &view, GraphImage mask,
                                    std::span<const std::uint32_t> selected) {
  m_selected.assign(selected.begin(), selected.end());
  std::ranges::sort(m_selected);
  const auto duplicates = std::ranges::unique(m_selected);
  m_selected.erase(duplicates.begin(), duplicates.end());
  if (m_selected.empty()) {
    return;
  }
  const glm::uvec2 size = graph.imageDesc(mask).size;
  ViewState &v = prepareFrame(graph, view, size);
  // The mask keeps every selected silhouette, occluded or not, so culling filters by selection
  // as well as by the frustum; an off-screen selection has no outline to draw anyway.
  std::optional<CullJob> job = addCullPass(graph, v, true);
  graph.addPass(
      "selection mask",
      [&](PassBuilder &builder) { builder.color(mask, rhi::LoadOp::Clear, {0.0f, 0.0f, 0.0f, 0.0f}); },
      [this, &v, job](rhi::ICommandList &commands, const PassResources &resources) {
        ensureFrameUploaded(v, resources);
        const RenderStatistics before = v.statistics;
        if (job) {
          recordIndirect(commands, v, *job, v.allBatches, m_maskPipelines);
        }
        v.statistics.indirectCallCount = before.indirectCallCount;
      });
}

std::optional<Renderer::CullJob> Renderer::addCullPass(RenderGraph &graph, ViewState &v, bool selectedOnly) {
  const SceneView &view = *v.view;
  const glm::uvec2 size = v.targetSize;
  std::optional<CullJob> job = reserveCullJob(v, false);
  if (!job) {
    return job;
  }
  const float aspect = static_cast<float>(size.x) / static_cast<float>(std::max(size.y, 1u));
  job->viewProjection = view.camera.projection(aspect) * view.camera.view();
  job->selectedOnly = selectedOnly;
  v.cullJobs.push_back(*job);
  graph.addPass(
      selectedOnly ? "selection mask cull" : "id cull", [](PassBuilder &) {},
      [this, &v](rhi::ICommandList &commands, const PassResources &resources) {
        ensureFrameUploaded(v, resources);
        recordCulling(commands, v);
      });
  return job;
}

void Renderer::addOutlinePass(RenderGraph &graph, GraphImage color, GraphImage mask, glm::vec4 outlineColor) {
  m_outlineColor = outlineColor;
  if (m_selected.empty()) {
    return;
  }
  graph.addPass(
      "outline",
      [&](PassBuilder &builder) {
        builder.color(color, rhi::LoadOp::Load);
        builder.sample(mask);
      },
      [this, mask](rhi::ICommandList &commands, const PassResources &resources) {
        recordOutline(commands, resources.image(mask));
      });
}

void Renderer::recordOutline(rhi::ICommandList &commands, rhi::ImageHandle mask) {
  SONNET_ZONE();
  const OutlineConstants constants{.color = m_outlineColor};
  commands.bindPipeline(m_outlinePipeline);
  const rhi::ImageBinding binding{.binding = rhi::PassImageBinding, .image = mask};
  commands.bindImages({&binding, 1});
  commands.pushConstants(std::as_bytes(std::span{&constants, 1}));
  commands.draw(3);
}

void Renderer::addDebugLinePass(RenderGraph &graph, const SceneView &view, GraphImage color, GraphImage depth) {
  if (view.debugLines.empty()) {
    return;
  }
  const glm::uvec2 size = graph.imageDesc(color).size;
  graph.addPass(
      "debug lines",
      [&](PassBuilder &builder) {
        builder.color(color, rhi::LoadOp::Load);
        builder.depth(depth, rhi::LoadOp::Load);
      },
      [this, &view, size](rhi::ICommandList &commands, const PassResources &) {
        recordDebugLines(commands, view, size);
      });
}

void Renderer::recordDebugLines(rhi::ICommandList &commands, const SceneView &view, glm::uvec2 targetSize) {
  SONNET_ZONE();
  const std::uint64_t bytes = view.debugLines.size() * 2 * sizeof(DebugVertex);
  const rhi::TransientAllocation allocation = m_device.allocateTransient(bytes);
  if (allocation.data.empty()) {
    SONNET_LOG_WARN("{} debug lines do not fit the frame's transient memory", view.debugLines.size());
    return;
  }
  auto *vertices = reinterpret_cast<DebugVertex *>(allocation.data.data());
  for (const DebugLine &line : view.debugLines) {
    *vertices++ = {.position = glm::vec4{line.from, 1.0f}, .color = line.color};
    *vertices++ = {.position = glm::vec4{line.to, 1.0f}, .color = line.color};
  }
  const float aspect = static_cast<float>(targetSize.x) / static_cast<float>(std::max(targetSize.y, 1u));
  const DebugConstants constants{.viewProjection = view.camera.projection(aspect) * view.camera.view()};
  commands.bindPipeline(m_debugLinePipeline);
  const rhi::BufferBinding binding{
      .binding = rhi::PassStorageBinding, .buffer = allocation.buffer, .offset = allocation.offset, .size = bytes};
  commands.bindBuffers({&binding, 1});
  commands.pushConstants(std::as_bytes(std::span{&constants, 1}));
  commands.draw(static_cast<std::uint32_t>(view.debugLines.size() * 2));
}

} // namespace sonnet::renderer
