#pragma once

#include <sonnet/assets/AssetDatabase.h>
#include <sonnet/core/Uuid.h>
#include <sonnet/renderer/RenderGraph.h>
#include <sonnet/renderer/RenderTarget.h>
#include <sonnet/renderer/Renderer.h>
#include <sonnet/renderer/SceneView.h>
#include <sonnet/rhi/Device.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <vector>

namespace sonnet::editor {

// Renders materials on a sphere for the Assets panel's thumbnails (docs/editor.md, "Thumbnails"). A material is
// drawn as a view of its own in the editor's graph (ADR-0021), into a small target, lit by a studio sky the
// thumbnailer makes for itself, and copied into a readback buffer the way the Picker's id is: the picture is
// available FramesInFlight frames later, so nothing stalls. Main thread only.
class MaterialThumbnailer {
public:
  // Views declared per frame: each is a full set of scene passes, so this is the cost of a frame that renders.
  static constexpr std::size_t PerFrame = 4;

  // One material's picture, or none when the material could not be loaded. `ticket` is the one the request
  // carried, so the owner can tell an answer from one it has asked again since.
  struct Result {
    core::Uuid uuid;
    std::uint64_t ticket{0};
    std::optional<renderer::TextureData> picture;
  };

  MaterialThumbnailer(rhi::IDevice &device, renderer::Renderer &renderer, assets::AssetDatabase &assets);
  ~MaterialThumbnailer();
  MaterialThumbnailer(const MaterialThumbnailer &) = delete;
  MaterialThumbnailer &operator=(const MaterialThumbnailer &) = delete;

  // Queues a material, or the new ticket of one already queued. Makes the sphere and the sky on the first call, so
  // it belongs before the frame's views are declared, as does loading the material.
  void request(const core::Uuid &uuid, std::uint64_t ticket);
  // Drops a queued material; a picture already being copied is the owner's to ignore.
  void cancel(const core::Uuid &uuid);

  // Once per frame in the editor's graph, after the device's beginFrame: returns the pictures requested
  // FramesInFlight frames ago, then declares up to PerFrame of the queued materials. Nothing is declared while
  // assets are loading, since a material is rendered once its textures have arrived and not before.
  [[nodiscard]] std::vector<Result> render(renderer::RenderGraph &graph);

private:
  // What one of the PerFrame views keeps across frames: the renderer finds a view's state by the address of its
  // SceneView, so these stay where they are.
  struct Slot {
    Slot(rhi::IDevice &device, std::size_t index);
    renderer::RenderTarget target;
    renderer::DrawItem draw;
    renderer::SceneView view;
  };
  struct Job {
    core::Uuid uuid;
    std::uint64_t ticket{0};
    bool material{false}; // false when it failed to load: nothing was copied
  };
  struct Frame {
    std::array<rhi::BufferHandle, PerFrame> buffers;
    std::array<Job, PerFrame> jobs;
    std::size_t count{0};
  };
  struct Queued {
    core::Uuid uuid;
    std::uint64_t ticket{0};
  };

  void createResources();

  rhi::IDevice &m_device;
  renderer::Renderer &m_renderer;
  assets::AssetDatabase &m_assets;
  std::deque<Queued> m_queue;
  std::vector<std::unique_ptr<Slot>> m_slots;
  std::array<Frame, rhi::FramesInFlight> m_frames;
  renderer::MeshHandle m_sphere;
  renderer::EnvironmentHandle m_sky;
  std::uint64_t m_frame{0};
};

} // namespace sonnet::editor
