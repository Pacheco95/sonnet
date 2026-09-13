#include <sonnet/renderer/Renderer.h>

#include <sonnet/core/Assert.h>
#include <sonnet/core/Error.h>
#include <sonnet/core/File.h>
#include <sonnet/core/Log.h>
#include <sonnet/core/Profile.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <format>
#include <type_traits>
#include <utility>

namespace sonnet::renderer {

namespace {

// Mirrors of shaders/sonnet.slang under scalar block layout.

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
  float padding[3];
};
static_assert(sizeof(GpuLight) == 64);

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
  std::uint32_t padding;
  std::uint64_t materials;
  std::uint64_t lights;
  std::uint64_t clusters;
};
static_assert(sizeof(FrameConstants) == 776);

struct ObjectData {
  glm::mat4 model;
  glm::mat4 normalMatrix;
  glm::vec4 color;
  std::uint32_t id;
  std::uint32_t material;
  std::uint32_t padding[2];
};
static_assert(sizeof(ObjectData) == 160);

struct DrawConstants {
  std::uint64_t vertices;
  std::uint32_t objectIndex;
  std::uint32_t cascade;
};
static_assert(sizeof(DrawConstants) == 16);

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
  defineGraphics(m_outlinePipeline, "outline",
                 {.colorFormats = {ColorFormat}, .cullMode = rhi::CullMode::None, .debugName = "outline"});
  defineCompute(m_clusterPipeline, "cluster", "computeMain", "light clustering");
  defineCompute(m_equirectPipeline, "ibl", "equirectToCube", "equirect to cube");
  defineCompute(m_cubeMipPipeline, "ibl", "cubeMip", "cube mip");
  defineCompute(m_irradiancePipeline, "ibl", "irradiance", "irradiance");
  defineCompute(m_prefilterPipeline, "ibl", "prefilter", "prefilter");
  defineCompute(m_brdfLutPipeline, "ibl", "brdfLut", "brdf lut");
  createPipelines(shaderDir);
  createDefaults();
  SONNET_LOG_DEBUG("renderer ready, shaders from {}", shaderDir.string());
}

Renderer::~Renderer() {
  m_meshes.forEach([this](MeshHandle handle, Mesh &mesh) {
    SONNET_LOG_WARN("leaked mesh \"{}\" ({}:{})", mesh.debugName, handle.index, handle.generation);
    m_device.destroyBuffer(mesh.vertices);
    m_device.destroyBuffer(mesh.indices);
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
  m_device.destroyImage(m_brdfLut);
  m_device.destroySampler(m_shadowSampler);
  m_device.destroySampler(m_linearClampSampler);
  for (const rhi::SamplerHandle sampler : m_materialSamplers) {
    m_device.destroySampler(sampler);
  }
  for (const rhi::PipelineHandle pipeline :
       {m_brdfLutPipeline, m_prefilterPipeline, m_irradiancePipeline, m_cubeMipPipeline, m_equirectPipeline,
        m_clusterPipeline, m_outlinePipeline, m_fxaaPipeline, m_tonemapPipeline, m_bloomUpPipeline, m_bloomDownPipeline,
        m_skyboxPipeline}) {
    m_device.destroyPipeline(pipeline);
  }
  for (const auto &pair :
       {m_maskPipelines, m_idPipelines, m_blendPipelines, m_forwardPipelines, m_shadowPipelines, m_depthPipelines}) {
    m_device.destroyPipeline(pair[1]);
    m_device.destroyPipeline(pair[0]);
  }
}

std::span<const std::string_view> Renderer::shaderNames() noexcept {
  static constexpr std::array<std::string_view, 8> names{"cluster", "depth",   "forward", "ibl",
                                                         "id",      "outline", "post",    "skybox"};
  return names;
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
    const std::filesystem::path path = shaderDir / std::format("{}.spv", name);
    const auto spirv = core::readFile(path);
    if (!spirv) {
      throw core::Exception{spirv.error()};
    }
    const rhi::ShaderHandle shader = m_device.createShader({.spirv = *spirv, .debugName = std::string{name}});
    for (const PipelineSlot &slot : m_pipelineSlots) {
      if (slot.shader == name) {
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
  std::vector<Submesh> submeshes = data.submeshes;
  if (submeshes.empty()) {
    submeshes.push_back(Submesh{0, static_cast<std::uint32_t>(data.indices.size()), 0});
  }
  const MeshHandle handle =
      m_meshes.emplace(Mesh{std::move(debugName), vertices, indices, std::move(submeshes), data.bounds()});
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
  SONNET_LOG_DEBUG("texture \"{}\": {}x{}, {} levels{}", m_textures.get(handle).debugName, data.size.x, data.size.y,
                   data.mipLevels, data.cube ? ", cube" : "");
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
  // way, so the graph orders the reads after the writes.
  m_frameImages = {};
  m_frameImages.lut = graph.importImage(m_brdfLut, rhi::ImageLayout::General,
                                        m_brdfLutPending ? rhi::ImageLayout::Undefined : rhi::ImageLayout::General);
  if (Environment *environment = m_environments.find(view.environment)) {
    const rhi::ImageLayout initial = environment->pending ? rhi::ImageLayout::Undefined : rhi::ImageLayout::General;
    m_frameImages.skybox = graph.importImage(environment->skybox, rhi::ImageLayout::General, initial);
    m_frameImages.irradiance = graph.importImage(environment->irradiance, rhi::ImageLayout::General, initial);
    m_frameImages.prefiltered = graph.importImage(environment->prefiltered, rhi::ImageLayout::General, initial);
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
      addEnvironmentPasses(graph, handle, environment, handle == view.environment);
    }
  });
}

void Renderer::addEnvironmentPasses(RenderGraph &graph, EnvironmentHandle handle, Environment &environment,
                                    bool viewed) {
  environment.pending = false;
  const std::uint32_t sampler = m_device.samplerIndex(m_linearClampSampler);
  const GraphImage equirect = graph.importImage(environment.equirectangular, rhi::ImageLayout::ShaderReadOnly,
                                                rhi::ImageLayout::ShaderReadOnly);
  const GraphImage skybox =
      viewed ? m_frameImages.skybox : graph.importImage(environment.skybox, rhi::ImageLayout::General);
  const GraphImage irradiance =
      viewed ? m_frameImages.irradiance : graph.importImage(environment.irradiance, rhi::ImageLayout::General);
  const GraphImage prefiltered =
      viewed ? m_frameImages.prefiltered : graph.importImage(environment.prefiltered, rhi::ImageLayout::General);
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

void Renderer::computeCascades(const SceneView &view, float aspect) {
  const float nearPlane = view.camera.nearPlane;
  const float farPlane = std::max(m_settings.shadowDistance, nearPlane * 2.0f);
  const glm::mat4 cameraView = view.camera.view();
  const glm::vec3 lightDirection = glm::normalize(view.sun.direction);
  const glm::vec3 up = std::abs(lightDirection.y) > 0.99f ? glm::vec3{0.0f, 0.0f, 1.0f} : glm::vec3{0.0f, 1.0f, 0.0f};
  constexpr float Lambda = 0.75f; // between logarithmic and uniform splits
  float previousSplit = nearPlane;
  for (std::uint32_t c = 0; c < CascadeCount; ++c) {
    const float p = static_cast<float>(c + 1) / static_cast<float>(CascadeCount);
    const float logarithmic = nearPlane * std::pow(farPlane / nearPlane, p);
    const float uniform = nearPlane + (farPlane - nearPlane) * p;
    const float split = Lambda * logarithmic + (1.0f - Lambda) * uniform;

    // The slice's eight corners in world space, from a finite projection over the slice.
    const glm::mat4 sliceProjection = glm::perspectiveRH_ZO(view.camera.fovY, aspect, previousSplit, split);
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
    const float extension = farPlane; // casters this far behind the slice still count
    const glm::mat4 lightView = glm::lookAt(centre - lightDirection * (radius + extension), centre, up);
    glm::mat4 lightProjection = orthoReversedZ(-radius, radius, -radius, radius, 0.0f, 2.0f * radius + extension);
    const float texelsPerUnit = static_cast<float>(m_settings.shadowMapSize) / (2.0f * radius);
    const glm::vec4 origin = lightProjection * lightView * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f};
    const glm::vec2 snapped = glm::round(glm::vec2{origin} * texelsPerUnit * 0.5f) / (texelsPerUnit * 0.5f);
    const glm::vec2 offset = snapped - glm::vec2{origin};
    lightProjection[3][0] += offset.x;
    lightProjection[3][1] += offset.y;
    m_cascades[c] = Cascade{lightProjection * lightView, split};
    previousSplit = split;
  }
}

void Renderer::prepareFrame(const RenderGraph &graph, const SceneView &view, glm::uvec2 targetSize) {
  SONNET_ZONE();
  if (m_view == &view && m_graph == &graph && m_graphFrame == graph.frameIndex() && m_targetSize == targetSize) {
    return;
  }
  m_view = &view;
  m_graph = &graph;
  m_graphFrame = graph.frameIndex();
  m_targetSize = targetSize;
  m_cascadesActive = false;
  m_frameBuffers = {};
  m_statistics = {};
  m_resolved.clear();
  m_opaqueOrder.clear();
  m_blendedOrder.clear();
  m_allOrder.clear();
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
    m_resolved.push_back(ResolvedDraw{.objectIndex = static_cast<std::uint32_t>(i),
                                      .mesh = mesh,
                                      .submesh = mesh->submeshes[item.submesh],
                                      .doubleSided = desc.doubleSided,
                                      .blended = desc.alphaMode == AlphaMode::Blend,
                                      .masked = desc.alphaMode == AlphaMode::Mask,
                                      .viewDepth = -viewPosition.z});
  }
  for (std::uint32_t i = 0; i < m_resolved.size(); ++i) {
    m_allOrder.push_back(i);
    (m_resolved[i].blended ? m_blendedOrder : m_opaqueOrder).push_back(i);
  }
  // Opaque draws grouped by pipeline, then by mesh so index buffers stay bound; blended ones
  // from the farthest to the nearest.
  std::ranges::sort(m_opaqueOrder, [&](std::uint32_t a, std::uint32_t b) {
    const ResolvedDraw &da = m_resolved[a];
    const ResolvedDraw &db = m_resolved[b];
    if (da.doubleSided != db.doubleSided) {
      return !da.doubleSided;
    }
    return da.mesh < db.mesh;
  });
  std::ranges::sort(m_blendedOrder, [&](std::uint32_t a, std::uint32_t b) {
    return m_resolved[a].viewDepth > m_resolved[b].viewDepth;
  });
}

void Renderer::ensureFrameUploaded(const PassResources &resources) {
  if (m_frameBuffers.uploaded || m_view == nullptr) {
    return;
  }
  m_frameBuffers.uploaded = true;
  const SceneView &view = *m_view;
  const std::uint32_t lightCount = static_cast<std::uint32_t>(std::min<std::size_t>(view.lights.size(), MaxLights));
  const std::uint32_t materialCount = m_materialSlots + 1;
  m_frameBuffers.frame = m_device.allocateTransient(sizeof(FrameConstants));
  m_frameBuffers.objects = m_device.allocateTransient(std::max<std::size_t>(view.draws.size(), 1) * sizeof(ObjectData));
  const rhi::TransientAllocation materials = m_device.allocateTransient(materialCount * sizeof(GpuMaterial));
  const rhi::TransientAllocation lights = m_device.allocateTransient(std::max(lightCount, 1u) * sizeof(GpuLight));
  if (!m_frameBuffers.valid() || materials.data.empty() || lights.data.empty()) {
    m_frameBuffers = {}; // the allocator logged the exhaustion; the passes draw nothing
    m_frameBuffers.uploaded = true;
    return;
  }

  // Objects, in the draw list's order so the object index is the draw index.
  auto *objects = reinterpret_cast<ObjectData *>(m_frameBuffers.objects.data.data());
  for (std::size_t i = 0; i < view.draws.size(); ++i) {
    const DrawItem &item = view.draws[i];
    objects[i] = ObjectData{.model = item.transform,
                            .normalMatrix = glm::transpose(glm::inverse(item.transform)),
                            .color = item.color,
                            .id = item.id,
                            .material = materialIndex(item.material),
                            .padding = {}};
  }

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
                            .padding = {}};
  }
  m_statistics.lightCount = lightCount;

  const float aspect = static_cast<float>(m_targetSize.x) / static_cast<float>(std::max(m_targetSize.y, 1u));
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
      .cameraPosition = glm::vec4{view.camera.position, 1.0f},
      .sunDirection = glm::vec4{glm::normalize(view.sun.direction), view.hasSun ? 1.0f : 0.0f},
      .sunColor = glm::vec4{view.sun.color * view.sun.intensity, 0.0f},
      .ambient = glm::vec4{view.ambient, 0.0f},
      .environment = glm::vec4{view.environmentIntensity, static_cast<float>(m_settings.prefilteredLevels),
                               view.exposure, environmentReady ? 1.0f : 0.0f},
      .clusterScaleBias =
          glm::vec4{static_cast<float>(m_targetSize.x) / ClusterGridX,
                    static_cast<float>(m_targetSize.y) / ClusterGridY, sliceScale, -sliceScale * std::log(nearPlane)},
      .targetSize = glm::vec2{m_targetSize},
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
      .padding = 0,
      .materials = m_device.bufferAddress(materials.buffer) + materials.offset,
      .lights = m_device.bufferAddress(lights.buffer) + lights.offset,
      .clusters = m_device.bufferAddress(m_clusterBuffer),
  };
  for (std::uint32_t c = 0; c < CascadeCount; ++c) {
    frame.cascadeMatrices[c] = m_cascades[c].matrix;
    frame.cascadeSplits[static_cast<int>(c)] = m_cascades[c].split;
    frame.cascadeImages[c] =
        m_cascadesActive ? sampledIndex(resources.image(m_cascadeImages[c])) : rhi::InvalidBindlessIndex;
  }
  std::memcpy(m_frameBuffers.frame.data.data(), &frame, sizeof(frame));
}

void Renderer::bindFrame(rhi::ICommandList &commands) {
  const std::array bindings{
      rhi::BufferBinding{.binding = rhi::PassUniformBinding,
                         .buffer = m_frameBuffers.frame.buffer,
                         .offset = m_frameBuffers.frame.offset,
                         .size = m_frameBuffers.frame.data.size()},
      rhi::BufferBinding{.binding = rhi::PassStorageBinding,
                         .buffer = m_frameBuffers.objects.buffer,
                         .offset = m_frameBuffers.objects.offset,
                         .size = m_frameBuffers.objects.data.size()},
  };
  commands.bindBuffers(bindings);
}

void Renderer::recordDraws(rhi::ICommandList &commands, std::span<const std::uint32_t> order,
                           std::span<const rhi::PipelineHandle, 2> pipelines, bool count, std::uint32_t cascade,
                           std::span<const std::uint32_t> only) {
  if (order.empty() || !m_frameBuffers.valid()) {
    return;
  }
  rhi::PipelineHandle bound;
  const Mesh *boundMesh = nullptr;
  for (const std::uint32_t index : order) {
    const ResolvedDraw &draw = m_resolved[index];
    if (!only.empty() && !std::ranges::binary_search(only, m_view->draws[draw.objectIndex].id)) {
      continue;
    }
    const rhi::PipelineHandle pipeline = pipelines[draw.doubleSided ? 1 : 0];
    if (pipeline != bound) {
      commands.bindPipeline(pipeline);
      bindFrame(commands);
      bound = pipeline;
    }
    if (draw.mesh != boundMesh) {
      commands.bindIndexBuffer(draw.mesh->indices, rhi::IndexType::Uint32);
      boundMesh = draw.mesh;
    }
    const DrawConstants push{m_device.bufferAddress(draw.mesh->vertices), draw.objectIndex, cascade};
    commands.pushConstants(std::as_bytes(std::span{&push, 1}));
    commands.drawIndexed(draw.submesh.indexCount, 1, draw.submesh.firstIndex);
    if (count) {
      ++m_statistics.drawCount;
      m_statistics.triangleCount += draw.submesh.indexCount / 3;
    }
  }
}

void Renderer::recordClustering(rhi::ICommandList &commands) {
  if (!m_frameBuffers.valid()) {
    return;
  }
  // The previous frame's fragments may still read the clusters this frame overwrites.
  commands.memoryBarrier({.srcStage = rhi::PipelineStage::FragmentShader,
                          .srcAccess = rhi::Access::ShaderRead,
                          .dstStage = rhi::PipelineStage::ComputeShader,
                          .dstAccess = rhi::Access::ShaderWrite});
  commands.bindPipeline(m_clusterPipeline);
  bindFrame(commands);
  commands.dispatch(groups(ClusterGridX, 4), groups(ClusterGridY, 3), groups(ClusterGridZ, 4));
  commands.memoryBarrier({.srcStage = rhi::PipelineStage::ComputeShader,
                          .srcAccess = rhi::Access::ShaderWrite,
                          .dstStage = rhi::PipelineStage::FragmentShader,
                          .dstAccess = rhi::Access::ShaderRead});
}

void Renderer::recordPost(rhi::ICommandList &commands, rhi::PipelineHandle pipeline, rhi::ImageHandle source,
                          rhi::ImageHandle secondary, glm::uvec2 targetSize) {
  const glm::uvec2 sourceSize = m_device.imageDesc(source).size;
  const PostConstants push{.source = sampledIndex(source),
                           .secondary = secondary ? sampledIndex(secondary) : rhi::InvalidBindlessIndex,
                           .sampler = m_device.samplerIndex(m_linearClampSampler),
                           .sourceTexel = 1.0f / glm::vec2{sourceSize},
                           .targetSize = glm::vec2{targetSize},
                           .exposure = m_view != nullptr ? m_view->exposure : 1.0f,
                           .bloomStrength = m_view != nullptr ? m_view->bloomStrength : 0.0f};
  commands.bindPipeline(pipeline);
  commands.pushConstants(std::as_bytes(std::span{&push, 1}));
  commands.draw(3);
}

void Renderer::addBloomPasses(RenderGraph &graph, GraphImage hdr, glm::uvec2 size, GraphImage &result) {
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
        [this, source, target = sizes[i]](rhi::ICommandList &commands, const PassResources &resources) {
          recordPost(commands, m_bloomDownPipeline, resources.image(source), {}, target);
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
        [this, source = down[level], below = up[level + 1], target = sizes[level]](rhi::ICommandList &commands,
                                                                                   const PassResources &resources) {
          recordPost(commands, m_bloomUpPipeline, resources.image(source), resources.image(below), target);
        });
  }
  result = up[0];
}

void Renderer::addScenePasses(RenderGraph &graph, const SceneView &view, GraphImage color, GraphImage depth,
                              glm::vec4 clearColor) {
  SONNET_ZONE();
  addPrecomputePasses(graph, view);
  const glm::uvec2 size = graph.imageDesc(color).size;
  prepareFrame(graph, view, size);

  // The cascades exist only with the scene passes: the id and mask passes on their own have no
  // shadow images to sample.
  m_cascadesActive = m_settings.shadows && view.hasSun && !m_opaqueOrder.empty();
  if (m_cascadesActive) {
    computeCascades(view, static_cast<float>(size.x) / static_cast<float>(std::max(size.y, 1u)));
    for (std::uint32_t c = 0; c < CascadeCount; ++c) {
      m_cascadeImages[c] = graph.createImage({.size = {m_settings.shadowMapSize, m_settings.shadowMapSize},
                                              .format = DepthFormat,
                                              .debugName = std::format("shadow cascade {}", c)});
      graph.addPass(
          std::format("shadow cascade {}", c),
          [&](PassBuilder &builder) { builder.depth(m_cascadeImages[c], rhi::LoadOp::Clear, 0.0f); },
          [this, c](rhi::ICommandList &commands, const PassResources &resources) {
            ensureFrameUploaded(resources);
            // Counted apart from the scene draws, which only the forward pass adds to.
            const RenderStatistics before = m_statistics;
            recordDraws(commands, m_opaqueOrder, m_shadowPipelines, true, c);
            m_statistics.shadowDrawCount = before.shadowDrawCount + (m_statistics.drawCount - before.drawCount);
            m_statistics.drawCount = before.drawCount;
            m_statistics.triangleCount = before.triangleCount;
          });
    }
  }

  graph.addPass(
      "depth", [&](PassBuilder &builder) { builder.depth(depth, rhi::LoadOp::Clear, 0.0f); },
      [this](rhi::ICommandList &commands, const PassResources &resources) {
        ensureFrameUploaded(resources);
        recordDraws(commands, m_opaqueOrder, m_depthPipelines, false);
      });
  graph.addPass(
      "light clustering", [](PassBuilder &) {},
      [this](rhi::ICommandList &commands, const PassResources &resources) {
        ensureFrameUploaded(resources);
        recordClustering(commands);
      });

  const GraphImage hdr = graph.createImage({.size = size, .format = HdrFormat, .debugName = "scene hdr"});
  const bool skybox = m_environments.contains(view.environment) && !m_environments.get(view.environment).pending;
  graph.addPass(
      "forward",
      [&](PassBuilder &builder) {
        builder.color(hdr, rhi::LoadOp::Clear, clearColor);
        builder.depth(depth, rhi::LoadOp::Load);
        if (m_cascadesActive) {
          for (const GraphImage cascade : m_cascadeImages) {
            builder.sample(cascade);
          }
        }
        builder.sample(m_frameImages.lut);
        if (skybox) {
          builder.sample(m_frameImages.skybox);
          builder.sample(m_frameImages.irradiance);
          builder.sample(m_frameImages.prefiltered);
        }
      },
      [this, skybox](rhi::ICommandList &commands, const PassResources &resources) {
        ensureFrameUploaded(resources);
        recordDraws(commands, m_opaqueOrder, m_forwardPipelines, true);
        if (skybox && m_frameBuffers.valid()) {
          commands.bindPipeline(m_skyboxPipeline);
          bindFrame(commands);
          commands.draw(3);
        }
        recordDraws(commands, m_blendedOrder, m_blendPipelines, true);
      });

  GraphImage bloom;
  if (m_settings.bloom && size.x >= 2 && size.y >= 2) {
    addBloomPasses(graph, hdr, size, bloom);
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
      [this, hdr, bloom, size](rhi::ICommandList &commands, const PassResources &resources) {
        recordPost(commands, m_tonemapPipeline, resources.image(hdr),
                   bloom.isValid() ? resources.image(bloom) : rhi::ImageHandle{}, size);
      });
  if (m_settings.antialiasing) {
    graph.addPass(
        "fxaa",
        [&](PassBuilder &builder) {
          builder.sample(ldr);
          builder.color(color, rhi::LoadOp::DontCare);
        },
        [this, ldr, size](rhi::ICommandList &commands, const PassResources &resources) {
          recordPost(commands, m_fxaaPipeline, resources.image(ldr), {}, size);
        });
  }
}

void Renderer::addIdPass(RenderGraph &graph, const SceneView &view, GraphImage ids, GraphImage depth) {
  prepareFrame(graph, view, graph.imageDesc(ids).size);
  graph.addPass(
      "id",
      [&](PassBuilder &builder) {
        builder.color(ids, rhi::LoadOp::Clear, {0.0f, 0.0f, 0.0f, 0.0f});
        builder.depth(depth, rhi::LoadOp::Load, 0.0f, rhi::StoreOp::DontCare);
      },
      [this](rhi::ICommandList &commands, const PassResources &resources) {
        ensureFrameUploaded(resources);
        // Editor-only work; the statistics keep counting the scene, not the picking pass.
        recordDraws(commands, m_allOrder, m_idPipelines, false);
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
  prepareFrame(graph, view, graph.imageDesc(mask).size);
  graph.addPass(
      "selection mask",
      [&](PassBuilder &builder) { builder.color(mask, rhi::LoadOp::Clear, {0.0f, 0.0f, 0.0f, 0.0f}); },
      [this](rhi::ICommandList &commands, const PassResources &resources) {
        ensureFrameUploaded(resources);
        recordDraws(commands, m_allOrder, m_maskPipelines, false, 0, m_selected);
      });
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

} // namespace sonnet::renderer
