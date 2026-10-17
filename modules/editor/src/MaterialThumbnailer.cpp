#include <sonnet/editor/MaterialThumbnailer.h>

#include <sonnet/assets/Thumbnail.h>
#include <sonnet/core/Profile.h>
#include <sonnet/renderer/Primitives.h>

#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>

namespace sonnet::editor {

namespace {

constexpr glm::uvec2 PictureSize{assets::ThumbnailSize, assets::ThumbnailSize};
constexpr std::uint64_t PictureBytes = std::uint64_t{PictureSize.x} * PictureSize.y * 4;

// The sky every material is lit by: a grey gradient from a dim floor to a brighter zenith, a large soft box
// above and in front of the sphere on its left, and a cooler, dimmer fill on its right. Equirectangular, in the
// mapping of `equirectUv` (shaders/ibl.slang), so the camera at +Z looking down -Z sees its horizon behind the
// sphere.
renderer::TextureData studioSky() {
  constexpr std::uint32_t width = 128;
  constexpr std::uint32_t height = 64;
  constexpr float pi = 3.14159265358979f;
  const glm::vec3 key = glm::normalize(glm::vec3{-0.55f, 0.7f, 0.65f});
  const glm::vec3 fill = glm::normalize(glm::vec3{0.8f, 0.15f, 0.6f});
  renderer::TextureData sky{.size = {width, height},
                            .format = rhi::Format::R16G16B16A16Sfloat,
                            .mipLevels = 1,
                            .cube = false,
                            .data = std::vector<std::byte>(std::size_t{width} * height * 4 * sizeof(std::uint16_t))};
  auto *out = reinterpret_cast<std::uint16_t *>(sky.data.data());
  for (std::uint32_t y = 0; y < height; ++y) {
    const float elevation = (0.5f - (static_cast<float>(y) + 0.5f) / static_cast<float>(height)) * pi;
    for (std::uint32_t x = 0; x < width; ++x) {
      const float azimuth = ((static_cast<float>(x) + 0.5f) / static_cast<float>(width) - 0.5f) * 2.0f * pi;
      const glm::vec3 direction{std::cos(elevation) * std::cos(azimuth), std::sin(elevation),
                                std::cos(elevation) * std::sin(azimuth)};
      const float up = std::clamp(direction.y, -1.0f, 1.0f);
      glm::vec3 radiance = up < 0.0f ? glm::mix(glm::vec3{0.3f}, glm::vec3{0.06f}, -up)
                                     : glm::mix(glm::vec3{0.3f}, glm::vec3{0.55f, 0.6f, 0.7f}, up);
      radiance += glm::vec3{1.0f, 0.97f, 0.92f} * 5.0f * glm::smoothstep(0.88f, 0.92f, glm::dot(direction, key));
      radiance += glm::vec3{0.8f, 0.9f, 1.0f} * 1.2f * glm::smoothstep(0.78f, 0.84f, glm::dot(direction, fill));
      std::uint16_t *texel = out + (std::size_t{y} * width + x) * 4;
      texel[0] = glm::packHalf1x16(radiance.r);
      texel[1] = glm::packHalf1x16(radiance.g);
      texel[2] = glm::packHalf1x16(radiance.b);
      texel[3] = glm::packHalf1x16(1.0f);
    }
  }
  return sky;
}

} // namespace

MaterialThumbnailer::Slot::Slot(rhi::IDevice &device, std::size_t index)
    : target(device, std::format("material thumbnail {}", index)) {
  target.resize(PictureSize);
  // The sphere fills the picture: its radius over the distance is the sine of half the field of view.
  view.camera.position = {0.0f, 0.0f, 2.2f};
  view.camera.fovY = glm::radians(30.0f);
  view.camera.bloomStrength = 0.0f;
  view.hasSun = false; // the sun's cascades are 64 MiB of shadow maps a thumbnail does not need
  view.bloomStrength = 0.0f;
  view.resetHistory = true; // every picture is a cut: nothing of the last material is a guide to this one
  view.draws = {&draw, 1};
}

MaterialThumbnailer::MaterialThumbnailer(rhi::IDevice &device, renderer::Renderer &renderer,
                                         assets::AssetDatabase &assets)
    : m_device(device), m_renderer(renderer), m_assets(assets) {
}

MaterialThumbnailer::~MaterialThumbnailer() {
  for (Frame &frame : m_frames) {
    for (const rhi::BufferHandle buffer : frame.buffers) {
      if (buffer) {
        m_device.destroyBuffer(buffer);
      }
    }
  }
  if (m_sky) {
    m_renderer.destroyEnvironment(m_sky);
  }
  if (m_sphere) {
    m_renderer.destroyMesh(m_sphere);
  }
}

void MaterialThumbnailer::request(const core::Uuid &uuid, std::uint64_t ticket) {
  // Here and not when the views are declared: a mesh made in the middle of a graph frame can move the meshes
  // the scene views have already batched.
  if (m_slots.empty()) {
    createResources();
  }
  const auto queued = std::ranges::find(m_queue, uuid, &Queued::uuid);
  if (queued != m_queue.end()) {
    queued->ticket = ticket;
    return;
  }
  m_queue.push_back({uuid, ticket});
}

void MaterialThumbnailer::cancel(const core::Uuid &uuid) {
  std::erase_if(m_queue, [&](const Queued &queued) { return queued.uuid == uuid; });
}

void MaterialThumbnailer::createResources() {
  m_sphere = m_renderer.createMesh(renderer::primitives::sphere(0.5f, 48, 24), "material thumbnail sphere");
  m_sky = m_renderer.createEnvironment(studioSky(), "material thumbnail sky");
  for (std::size_t i = 0; i < PerFrame; ++i) {
    auto slot = std::make_unique<Slot>(m_device, i);
    slot->draw.mesh = m_sphere;
    slot->view.environment = m_sky;
    m_slots.push_back(std::move(slot));
  }
  for (Frame &frame : m_frames) {
    for (rhi::BufferHandle &buffer : frame.buffers) {
      buffer = m_device.createBuffer({.size = PictureBytes,
                                      .usage = rhi::BufferUsage::TransferDst,
                                      .memory = rhi::MemoryUsage::GpuToCpu,
                                      .debugName = "material thumbnail readback"});
    }
  }
}

std::vector<MaterialThumbnailer::Result> MaterialThumbnailer::render(renderer::RenderGraph &graph) {
  SONNET_ZONE();
  // The frame slot beginFrame just waited for: its copies, made FramesInFlight frames ago, are in the buffers.
  Frame &frame = m_frames[m_frame % rhi::FramesInFlight];
  ++m_frame;
  std::vector<Result> done;
  for (std::size_t i = 0; i < frame.count; ++i) {
    const Job &job = frame.jobs[i];
    Result result{.uuid = job.uuid, .ticket = job.ticket, .picture = std::nullopt};
    if (job.material) {
      const std::span<const std::byte> pixels = m_device.mappedRange(frame.buffers[i]);
      renderer::TextureData picture{.size = PictureSize,
                                    .format = rhi::Format::R8G8B8A8Unorm,
                                    .mipLevels = 1,
                                    .cube = false,
                                    .data = {pixels.begin(), pixels.begin() + PictureBytes}};
      for (std::size_t alpha = 3; alpha < picture.data.size(); alpha += 4) {
        picture.data[alpha] = std::byte{255}; // the scene's alpha is not coverage
      }
      result.picture = std::move(picture);
    }
    done.push_back(std::move(result));
  }
  frame.count = 0;

  if (m_queue.empty() || m_assets.loading()) {
    return done;
  }
  while (frame.count < PerFrame && !m_queue.empty()) {
    const Queued queued = m_queue.front();
    m_queue.pop_front();
    Job &job = frame.jobs[frame.count];
    job = {.uuid = queued.uuid, .ticket = queued.ticket, .material = false};
    const renderer::MaterialHandle material = m_assets.material(queued.uuid);
    if (!material) {
      ++frame.count; // reported next time round, without a picture
      continue;
    }
    Slot &slot = *m_slots[frame.count];
    slot.draw.material = material;
    const renderer::GraphImage color = graph.importImage(slot.target.color());
    const renderer::GraphImage depth = graph.importImage(slot.target.depth());
    m_renderer.addScenePasses(graph, slot.view, color, depth);
    const rhi::BufferHandle buffer = frame.buffers[frame.count];
    graph.addPass(
        "material thumbnail readback", [&](renderer::PassBuilder &builder) { builder.transferSrc(color); },
        [color, buffer](rhi::ICommandList &commands, const renderer::PassResources &resources) {
          commands.copyImageToBuffer(resources.image(color), buffer);
        });
    job.material = true;
    ++frame.count;
  }
  return done;
}

} // namespace sonnet::editor
