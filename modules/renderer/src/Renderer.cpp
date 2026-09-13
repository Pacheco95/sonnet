#include <sonnet/renderer/Renderer.h>

#include <sonnet/core/Assert.h>
#include <sonnet/core/Error.h>
#include <sonnet/core/File.h>
#include <sonnet/core/Log.h>
#include <sonnet/core/Profile.h>

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
};
static_assert(sizeof(ObjectData) == 144);

struct DrawConstants {
  std::uint64_t vertices;
  std::uint32_t objectIndex;
  std::uint32_t padding{0};
};
static_assert(sizeof(DrawConstants) == 16);

} // namespace

Renderer::Renderer(rhi::IDevice &device, const std::filesystem::path &shaderDir) : m_device(device) {
  const std::filesystem::path path = shaderDir / "forward.spv";
  const auto spirv = core::readFile(path);
  if (!spirv) {
    throw core::Exception{spirv.error()};
  }
  const rhi::ShaderHandle shader = m_device.createShader({.spirv = *spirv, .debugName = "forward"});
  m_forwardPipeline = m_device.createGraphicsPipeline({.shader = shader,
                                                       .colorFormats = {ColorFormat},
                                                       .depthFormat = DepthFormat,
                                                       .depth = {.test = true, .write = true},
                                                       .cullMode = rhi::CullMode::Back,
                                                       .debugName = "forward"});
  m_device.destroyShader(shader);
  SONNET_LOG_DEBUG("renderer ready, shaders from {}", shaderDir.string());
}

Renderer::~Renderer() {
  m_meshes.forEach([this](MeshHandle handle, Mesh &mesh) {
    SONNET_LOG_WARN("leaked mesh \"{}\" ({}:{})", mesh.debugName, handle.index, handle.generation);
    m_device.destroyBuffer(mesh.vertices);
    m_device.destroyBuffer(mesh.indices);
  });
  m_device.destroyPipeline(m_forwardPipeline);
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

void Renderer::recordForward(rhi::ICommandList &commands, const SceneView &view, glm::uvec2 targetSize) {
  SONNET_ZONE();
  m_statistics = {};
  if (view.draws.empty() || targetSize.x == 0 || targetSize.y == 0) {
    return;
  }
  const rhi::TransientAllocation frameSlice = m_device.allocateTransient(sizeof(FrameConstants));
  const rhi::TransientAllocation objectSlice = m_device.allocateTransient(view.draws.size() * sizeof(ObjectData));
  if (frameSlice.data.empty() || objectSlice.data.empty()) {
    return; // the allocator logged the exhaustion
  }

  const float aspect = static_cast<float>(targetSize.x) / static_cast<float>(targetSize.y);
  const FrameConstants frame{
      .viewProjection = view.camera.projection(aspect) * view.camera.view(),
      .cameraPosition = glm::vec4{view.camera.position, 1.0f},
      .lightDirection = glm::vec4{glm::normalize(view.light.direction), 0.0f},
      .lightColor = glm::vec4{view.light.color, view.light.intensity},
      .ambient = glm::vec4{view.ambient, 0.0f},
  };
  std::memcpy(frameSlice.data.data(), &frame, sizeof(frame));

  auto *objects = reinterpret_cast<ObjectData *>(objectSlice.data.data());
  for (std::size_t i = 0; i < view.draws.size(); ++i) {
    const DrawItem &item = view.draws[i];
    objects[i] = ObjectData{item.transform, glm::transpose(glm::inverse(item.transform)), item.color};
  }

  commands.bindPipeline(m_forwardPipeline);
  const std::array bindings{
      rhi::BufferBinding{.binding = rhi::PassUniformBinding,
                         .buffer = frameSlice.buffer,
                         .offset = frameSlice.offset,
                         .size = frameSlice.data.size()},
      rhi::BufferBinding{.binding = rhi::PassStorageBinding,
                         .buffer = objectSlice.buffer,
                         .offset = objectSlice.offset,
                         .size = objectSlice.data.size()},
  };
  commands.bindBuffers(bindings);

  for (std::size_t i = 0; i < view.draws.size(); ++i) {
    const Mesh *mesh = m_meshes.find(view.draws[i].mesh);
    if (mesh == nullptr) {
      continue; // a stale handle draws nothing; the owner is expected to notice
    }
    const DrawConstants push{m_device.bufferAddress(mesh->vertices), static_cast<std::uint32_t>(i)};
    commands.pushConstants(std::as_bytes(std::span{&push, 1}));
    commands.bindIndexBuffer(mesh->indices, rhi::IndexType::Uint32);
    commands.drawIndexed(mesh->indexCount);
    ++m_statistics.drawCount;
    m_statistics.triangleCount += mesh->indexCount / 3;
  }
}

} // namespace sonnet::renderer
