#include <sonnet/renderer/Renderer.h>

#include <sonnet/core/Assert.h>
#include <sonnet/core/Error.h>
#include <sonnet/core/File.h>
#include <sonnet/core/Log.h>
#include <sonnet/core/Profile.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <format>

namespace sonnet::renderer {

namespace {

// Mirrors of shaders/sonnet.slang under scalar block layout.
struct FrameConstants {
  glm::mat4 viewProjection;
  glm::vec4 cameraPosition;
  glm::vec4 lightDirection;
  glm::vec4 lightColor;
  glm::vec4 ambient;
};
static_assert(sizeof(FrameConstants) == 128);

struct ObjectData {
  glm::mat4 model;
  glm::mat4 normalMatrix;
  glm::vec4 color;
  std::uint32_t id;
  std::uint32_t padding[3];
};
static_assert(sizeof(ObjectData) == 160);

struct DrawConstants {
  std::uint64_t vertices;
  std::uint32_t objectIndex;
  std::uint32_t padding{0};
};
static_assert(sizeof(DrawConstants) == 16);

// Mirror of shaders/outline.slang.
struct OutlineConstants {
  glm::vec4 color;
};
static_assert(sizeof(OutlineConstants) == 16);

} // namespace

Renderer::Renderer(rhi::IDevice &device, const std::filesystem::path &shaderDir) : m_device(device) {
  m_forwardPipeline = createPipeline(shaderDir, "forward",
                                     {.colorFormats = {ColorFormat},
                                      .depthFormat = DepthFormat,
                                      .depth = {.test = true, .write = true},
                                      .cullMode = rhi::CullMode::Back,
                                      .debugName = "forward"});
  // Reversed-Z GreaterOrEqual against the forward pass's depth keeps exactly the visible surface.
  m_idPipeline = createPipeline(shaderDir, "id",
                                {.colorFormats = {IdFormat},
                                 .depthFormat = DepthFormat,
                                 .depth = {.test = true, .write = false},
                                 .cullMode = rhi::CullMode::Back,
                                 .debugName = "id"});
  // The same id shader without a depth attachment: every selected surface, occluded or not.
  m_maskPipeline = createPipeline(
      shaderDir, "id", {.colorFormats = {IdFormat}, .cullMode = rhi::CullMode::Back, .debugName = "selection mask"});
  m_outlinePipeline = createPipeline(
      shaderDir, "outline", {.colorFormats = {ColorFormat}, .cullMode = rhi::CullMode::None, .debugName = "outline"});
  SONNET_LOG_DEBUG("renderer ready, shaders from {}", shaderDir.string());
}

Renderer::~Renderer() {
  m_meshes.forEach([this](MeshHandle handle, Mesh &mesh) {
    SONNET_LOG_WARN("leaked mesh \"{}\" ({}:{})", mesh.debugName, handle.index, handle.generation);
    m_device.destroyBuffer(mesh.vertices);
    m_device.destroyBuffer(mesh.indices);
  });
  m_device.destroyPipeline(m_outlinePipeline);
  m_device.destroyPipeline(m_maskPipeline);
  m_device.destroyPipeline(m_idPipeline);
  m_device.destroyPipeline(m_forwardPipeline);
}

rhi::PipelineHandle Renderer::createPipeline(const std::filesystem::path &shaderDir, const char *name,
                                             rhi::GraphicsPipelineDesc desc) {
  const std::filesystem::path path = shaderDir / std::format("{}.spv", name);
  const auto spirv = core::readFile(path);
  if (!spirv) {
    throw core::Exception{spirv.error()};
  }
  const rhi::ShaderHandle shader = m_device.createShader({.spirv = *spirv, .debugName = name});
  desc.shader = shader;
  const rhi::PipelineHandle pipeline = m_device.createGraphicsPipeline(desc);
  m_device.destroyShader(shader);
  return pipeline;
}

MeshHandle Renderer::createMesh(const MeshData &data, std::string debugName) {
  SONNET_ASSERT(!data.vertices.empty() && data.indices.size() % 3 == 0, "mesh \"{}\" is not a triangle list",
                debugName);
  const std::uint64_t vertexBytes = data.vertices.size() * sizeof(Vertex);
  const std::uint64_t indexBytes = data.indices.size() * sizeof(std::uint32_t);
  const rhi::BufferHandle vertices = m_device.createBuffer({.size = vertexBytes,
                                                            .usage = rhi::BufferUsage::Storage,
                                                            .memory = rhi::MemoryUsage::CpuToGpu,
                                                            .debugName = std::format("{} vertices", debugName)});
  const rhi::BufferHandle indices = m_device.createBuffer({.size = indexBytes,
                                                           .usage = rhi::BufferUsage::Index,
                                                           .memory = rhi::MemoryUsage::CpuToGpu,
                                                           .debugName = std::format("{} indices", debugName)});
  std::memcpy(m_device.mappedRange(vertices).data(), data.vertices.data(), vertexBytes);
  std::memcpy(m_device.mappedRange(indices).data(), data.indices.data(), indexBytes);
  const MeshHandle handle =
      m_meshes.emplace(Mesh{std::move(debugName), vertices, indices, static_cast<std::uint32_t>(data.indices.size())});
  SONNET_LOG_DEBUG("mesh \"{}\": {} vertices, {} triangles", m_meshes.get(handle).debugName, data.vertices.size(),
                   data.triangleCount());
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

void Renderer::addScenePasses(RenderGraph &graph, const SceneView &view, GraphImage color, GraphImage depth,
                              glm::vec4 clearColor) {
  graph.addPass(
      "forward",
      [&](PassBuilder &builder) {
        builder.color(color, rhi::LoadOp::Clear, clearColor);
        builder.depth(depth, rhi::LoadOp::Clear, 0.0f);
      },
      [this, &view, color](rhi::ICommandList &commands, const PassResources &resources) {
        recordForward(commands, view, m_device.imageDesc(resources.image(color)).size);
      });
}

void Renderer::addIdPass(RenderGraph &graph, const SceneView &view, GraphImage ids, GraphImage depth) {
  graph.addPass(
      "id",
      [&](PassBuilder &builder) {
        builder.color(ids, rhi::LoadOp::Clear, {0.0f, 0.0f, 0.0f, 0.0f});
        builder.depth(depth, rhi::LoadOp::Load, 0.0f, rhi::StoreOp::DontCare);
      },
      [this, &view, ids](rhi::ICommandList &commands, const PassResources &resources) {
        recordIds(commands, view, m_device.imageDesc(resources.image(ids)).size);
      });
}

void Renderer::addSelectionMaskPass(RenderGraph &graph, const SceneView &view, GraphImage mask,
                                    std::span<const std::uint32_t> selected) {
  m_selectedCount = static_cast<std::uint32_t>(std::min(selected.size(), MaxSelected));
  std::copy_n(selected.begin(), m_selectedCount, m_selected.begin());
  if (m_selectedCount == 0) {
    return;
  }
  graph.addPass(
      "selection mask",
      [&](PassBuilder &builder) { builder.color(mask, rhi::LoadOp::Clear, {0.0f, 0.0f, 0.0f, 0.0f}); },
      [this, &view, mask](rhi::ICommandList &commands, const PassResources &resources) {
        recordSelectionMask(commands, view, m_device.imageDesc(resources.image(mask)).size);
      });
}

void Renderer::addOutlinePass(RenderGraph &graph, GraphImage color, GraphImage mask, glm::vec4 outlineColor) {
  m_outlineColor = outlineColor;
  if (m_selectedCount == 0) {
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

Renderer::PassBuffers Renderer::uploadPassBuffers(const SceneView &view, glm::uvec2 targetSize) {
  PassBuffers buffers{.frame = m_device.allocateTransient(sizeof(FrameConstants)),
                      .objects = m_device.allocateTransient(view.draws.size() * sizeof(ObjectData))};
  if (!buffers.valid()) {
    return buffers; // the allocator logged the exhaustion
  }
  const float aspect = static_cast<float>(targetSize.x) / static_cast<float>(targetSize.y);
  const FrameConstants frame{
      .viewProjection = view.camera.projection(aspect) * view.camera.view(),
      .cameraPosition = glm::vec4{view.camera.position, 1.0f},
      .lightDirection = glm::vec4{glm::normalize(view.light.direction), 0.0f},
      .lightColor = glm::vec4{view.light.color, view.light.intensity},
      .ambient = glm::vec4{view.ambient, 0.0f},
  };
  std::memcpy(buffers.frame.data.data(), &frame, sizeof(frame));
  auto *objects = reinterpret_cast<ObjectData *>(buffers.objects.data.data());
  for (std::size_t i = 0; i < view.draws.size(); ++i) {
    const DrawItem &item = view.draws[i];
    objects[i] = ObjectData{item.transform, glm::transpose(glm::inverse(item.transform)), item.color, item.id, {}};
  }
  return buffers;
}

void Renderer::recordDraws(rhi::ICommandList &commands, const SceneView &view, const PassBuffers &buffers,
                           rhi::PipelineHandle pipeline, bool count, std::span<const std::uint32_t> only) {
  commands.bindPipeline(pipeline);
  const std::array bindings{
      rhi::BufferBinding{.binding = rhi::PassUniformBinding,
                         .buffer = buffers.frame.buffer,
                         .offset = buffers.frame.offset,
                         .size = buffers.frame.data.size()},
      rhi::BufferBinding{.binding = rhi::PassStorageBinding,
                         .buffer = buffers.objects.buffer,
                         .offset = buffers.objects.offset,
                         .size = buffers.objects.data.size()},
  };
  commands.bindBuffers(bindings);

  for (std::size_t i = 0; i < view.draws.size(); ++i) {
    if (!only.empty() && std::ranges::find(only, view.draws[i].id) == only.end()) {
      continue;
    }
    const Mesh *mesh = m_meshes.find(view.draws[i].mesh);
    if (mesh == nullptr) {
      continue; // a stale handle draws nothing; the owner is expected to notice
    }
    const DrawConstants push{m_device.bufferAddress(mesh->vertices), static_cast<std::uint32_t>(i)};
    commands.pushConstants(std::as_bytes(std::span{&push, 1}));
    commands.bindIndexBuffer(mesh->indices, rhi::IndexType::Uint32);
    commands.drawIndexed(mesh->indexCount);
    if (count) {
      ++m_statistics.drawCount;
      m_statistics.triangleCount += mesh->indexCount / 3;
    }
  }
}

void Renderer::recordForward(rhi::ICommandList &commands, const SceneView &view, glm::uvec2 targetSize) {
  SONNET_ZONE();
  m_statistics = {};
  if (view.draws.empty() || targetSize.x == 0 || targetSize.y == 0) {
    return;
  }
  const PassBuffers buffers = uploadPassBuffers(view, targetSize);
  if (buffers.valid()) {
    recordDraws(commands, view, buffers, m_forwardPipeline, true);
  }
}

void Renderer::recordIds(rhi::ICommandList &commands, const SceneView &view, glm::uvec2 targetSize) {
  SONNET_ZONE();
  if (view.draws.empty() || targetSize.x == 0 || targetSize.y == 0) {
    return;
  }
  const PassBuffers buffers = uploadPassBuffers(view, targetSize);
  if (buffers.valid()) {
    // Editor-only work; the statistics keep counting the scene, not the picking pass.
    recordDraws(commands, view, buffers, m_idPipeline, false);
  }
}

void Renderer::recordSelectionMask(rhi::ICommandList &commands, const SceneView &view, glm::uvec2 targetSize) {
  SONNET_ZONE();
  if (view.draws.empty() || targetSize.x == 0 || targetSize.y == 0) {
    return;
  }
  const PassBuffers buffers = uploadPassBuffers(view, targetSize);
  if (buffers.valid()) {
    recordDraws(commands, view, buffers, m_maskPipeline, false, std::span{m_selected.data(), m_selectedCount});
  }
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
