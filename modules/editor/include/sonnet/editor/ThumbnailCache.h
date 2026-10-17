#pragma once

#include <sonnet/assets/AssetDatabase.h>
#include <sonnet/core/JobSystem.h>
#include <sonnet/core/Math.h>
#include <sonnet/core/Uuid.h>
#include <sonnet/rhi/Device.h>
#include <sonnet/ui/ImGuiLayer.h>

#include <imgui.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>

namespace sonnet::editor {

// A picture ImGui can draw; texture 0 is none.
struct ThumbnailImage {
  ImTextureID texture{0};
  glm::uvec2 size{0};
};

// The Assets panel's thumbnails on the GPU (docs/editor.md, "Thumbnails"). `get` is called for a tile that is drawn:
// the first call for an asset schedules `assets::makeThumbnail` on the job system, and a later one returns the
// picture once `update` has uploaded it. A picture nobody has asked for in a while is released, so what is
// resident is about what the panel shows. Main thread only.
class ThumbnailCache {
public:
  // The most pictures resident at once: each holds a descriptor of Dear ImGui's pool (ImGuiLayer.cpp).
  static constexpr std::size_t MaxResident = 768;
  // Uploads per `update`, so a folder of hundreds of textures fills in over a few frames rather than in one.
  static constexpr std::size_t UploadsPerFrame = 8;
  // Frames a picture stays resident without being asked for.
  static constexpr std::uint64_t IdleFrames = 180;

  ThumbnailCache(rhi::IDevice &device, ui::ImGuiLayer &imgui, assets::AssetDatabase &assets);
  ~ThumbnailCache();
  ThumbnailCache(const ThumbnailCache &) = delete;
  ThumbnailCache &operator=(const ThumbnailCache &) = delete;

  // The picture of an asset: empty while it is being made, and for an asset that has none (the panel draws an icon).
  [[nodiscard]] ThumbnailImage get(const core::Uuid &uuid);
  // Once per frame: uploads what the workers finished and releases what has gone unused.
  void update();
  // Forgets the picture of an asset whose source changed; the next `get` makes it again.
  void invalidate(const core::Uuid &uuid);
  // Waits for every picture asked for so far and uploads them all, for captures and tests.
  void settle();
  [[nodiscard]] std::size_t resident() const noexcept {
    return m_resident;
  }
  // Whether a picture is still being made or waits for its upload.
  [[nodiscard]] bool busy() const noexcept;

private:
  struct Pending {
    std::atomic<bool> done{false};
    std::optional<renderer::TextureData> picture; // nothing when the asset has none or it failed
  };
  struct Entry {
    std::shared_ptr<Pending> pending; // null once uploaded
    core::JobHandle job;
    rhi::ImageHandle image;
    ImTextureID texture{0};
    glm::uvec2 size{0};
    std::uint64_t used{0};
  };

  void release(Entry &entry);
  void upload(Entry &entry);
  void drain(std::size_t budget);

  rhi::IDevice &m_device;
  ui::ImGuiLayer &m_imgui;
  assets::AssetDatabase &m_assets;
  std::map<core::Uuid, Entry> m_entries;
  std::uint64_t m_frame{0};
  std::size_t m_resident{0};
};

} // namespace sonnet::editor
