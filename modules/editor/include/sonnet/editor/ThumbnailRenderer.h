#pragma once

#include <sonnet/assets/AssetDatabase.h>
#include <sonnet/core/Uuid.h>
#include <sonnet/renderer/RenderGraph.h>
#include <sonnet/renderer/RenderTarget.h>
#include <sonnet/renderer/Renderer.h>
#include <sonnet/renderer/SceneView.h>
#include <sonnet/rhi/Device.h>
#include <sonnet/world/World.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <vector>

namespace sonnet::editor {

// Renders the Assets panel's thumbnails that are drawn by the engine (docs/editor.md, "Thumbnails"): a material on a
// sphere, a mesh, and a model or a prefab framed on its bounds. A subject is drawn as a view of its own in the
// editor's graph (ADR-0021), into a small target, lit by a studio sky the renderer makes for itself, and copied into
// a readback buffer the way the Picker's id is: the picture is available FramesInFlight frames later, so nothing
// stalls. A prefab is read from the world and never instantiated, so the open scene, the selection and the undo
// history are not touched. Main thread only.
class ThumbnailRenderer {
public:
  enum class Subject : std::uint8_t {
    Material, // on a sphere
    Mesh,     // with its own materials
    Prefab,   // a prefab's tree, which is also what a model is
  };

  // Views declared per frame: each is a full set of scene passes, so this is the cost of a frame that renders.
  static constexpr std::size_t PerFrame = 4;
  // Subjects queued at once: each one asks the database for its meshes and materials, which import on the job
  // system and are made on the main thread when they arrive, so the owner holds the rest back to keep that burst small.
  static constexpr std::size_t Backlog = 2 * PerFrame;

  // One subject's picture, or none when it could not be loaded. `ticket` is the one the request
  // carried, so the owner can tell an answer from one it has asked again since.
  struct Result {
    core::Uuid uuid;
    std::uint64_t ticket{0};
    std::optional<renderer::TextureData> picture;
  };

  ThumbnailRenderer(rhi::IDevice &device, renderer::Renderer &renderer, assets::AssetDatabase &assets,
                    const world::World &world);
  ~ThumbnailRenderer();
  ThumbnailRenderer(const ThumbnailRenderer &) = delete;
  ThumbnailRenderer &operator=(const ThumbnailRenderer &) = delete;

  // Queues a subject, or the new ticket of one already queued. Makes the sphere and the sky on the first call and
  // asks for the meshes and materials the subject draws, so it belongs before the frame's views are declared.
  void request(const core::Uuid &uuid, std::uint64_t ticket, Subject subject);
  [[nodiscard]] std::size_t queued() const noexcept {
    return m_queue.size();
  }
  // Drops a queued subject; a picture already being copied is the owner's to ignore.
  void cancel(const core::Uuid &uuid);

  // Once per frame in the editor's graph, after the device's beginFrame: returns the pictures requested
  // FramesInFlight frames ago, then declares up to PerFrame of the queued subjects. Nothing is declared while
  // assets are loading, since a subject is rendered once its meshes and textures have arrived and not before.
  [[nodiscard]] std::vector<Result> render(renderer::RenderGraph &graph);

private:
  // What one of the PerFrame views keeps across frames: the renderer finds a view's state by the address of its
  // SceneView, so these stay where they are.
  struct Slot {
    Slot(rhi::IDevice &device, std::size_t index);
    renderer::RenderTarget target;
    std::vector<renderer::DrawItem> draws;
    std::vector<glm::mat4> joints;
    std::vector<float> morphWeights;
    renderer::SceneView view;
  };
  struct Job {
    core::Uuid uuid;
    std::uint64_t ticket{0};
    bool drawn{false}; // false when it failed to load: nothing was copied
  };
  struct Frame {
    std::array<rhi::BufferHandle, PerFrame> buffers;
    std::array<Job, PerFrame> jobs;
    std::size_t count{0};
  };
  struct Queued {
    core::Uuid uuid;
    std::uint64_t ticket{0};
    Subject subject{Subject::Material};
  };

  void createResources();
  // The items a subject draws, asking the database for the meshes and materials it names; false when there are none,
  // which is how a subject that is still importing, or that failed to, shows.
  [[nodiscard]] bool gather(const Queued &queued, std::vector<renderer::DrawItem> &draws,
                            std::vector<glm::mat4> &joints, std::vector<float> &morphWeights);
  // Fills the slot's draws and places its camera; false when there is nothing to draw.
  [[nodiscard]] bool compose(Slot &slot, const Queued &queued);

  rhi::IDevice &m_device;
  renderer::Renderer &m_renderer;
  assets::AssetDatabase &m_assets;
  const world::World &m_world;
  std::deque<Queued> m_queue;
  std::vector<std::unique_ptr<Slot>> m_slots;
  std::array<Frame, rhi::FramesInFlight> m_frames;
  renderer::MeshHandle m_sphere;
  renderer::EnvironmentHandle m_sky;
  std::uint64_t m_frame{0};
};

} // namespace sonnet::editor
