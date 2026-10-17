#include <sonnet/editor/ThumbnailCache.h>

#include <sonnet/assets/Thumbnail.h>
#include <sonnet/core/Log.h>
#include <sonnet/core/Profile.h>

#include <algorithm>

namespace sonnet::editor {

ThumbnailCache::ThumbnailCache(rhi::IDevice &device, ui::ImGuiLayer &imgui, assets::AssetDatabase &assets)
    : m_device(device), m_imgui(imgui), m_assets(assets) {
}

ThumbnailCache::~ThumbnailCache() {
  for (auto &[uuid, entry] : m_entries) {
    if (entry.job.valid()) {
      m_assets.jobs().wait(entry.job); // the worker holds the request, not this cache, but the files are the project's
    }
    release(entry);
  }
}

ThumbnailImage ThumbnailCache::get(const core::Uuid &uuid) {
  auto found = m_entries.find(uuid);
  if (found == m_entries.end()) {
    Entry entry;
    entry.used = m_frame;
    if (const auto request = m_assets.thumbnailRequest(uuid)) {
      entry.pending = std::make_shared<Pending>();
      entry.job = m_assets.jobs().schedule("thumbnail", [request = *request, pending = entry.pending] {
        auto made = assets::makeThumbnail(request);
        if (made) {
          pending->picture = std::move(*made);
        } else {
          SONNET_LOG_WARN("{}: no thumbnail: {}", request.source.string(), made.error().toString());
        }
        pending->done.store(true, std::memory_order_release);
      });
    }
    found = m_entries.emplace(uuid, std::move(entry)).first;
  }
  found->second.used = m_frame;
  return {found->second.texture, found->second.size};
}

void ThumbnailCache::release(Entry &entry) {
  if (entry.texture != 0) {
    m_imgui.unregisterImage(entry.texture);
    m_device.destroyImage(entry.image);
    --m_resident;
  }
  entry = {};
}

void ThumbnailCache::upload(Entry &entry) {
  const renderer::TextureData &picture = *entry.pending->picture;
  entry.image = m_device.createImage({.size = picture.size,
                                      .format = picture.format,
                                      .usage = rhi::ImageUsage::Sampled | rhi::ImageUsage::TransferDst,
                                      .mipLevels = 1,
                                      .cube = false,
                                      .debugName = "thumbnail"});
  const rhi::ImageUpload level{.mipLevel = 0, .layer = 0, .data = picture.level(0)};
  m_device.uploadImage(entry.image, {&level, 1});
  entry.texture = m_imgui.registerImage(entry.image);
  entry.size = picture.size;
  ++m_resident;
}

void ThumbnailCache::drain(std::size_t budget) {
  std::size_t uploaded = 0;
  for (auto &[uuid, entry] : m_entries) {
    if (!entry.pending || !entry.pending->done.load(std::memory_order_acquire)) {
      continue;
    }
    if (entry.pending->picture) {
      if (uploaded == budget) {
        continue;
      }
      // Past the cap the tile keeps its icon until the pictures nobody looks at have been released.
      if (m_resident < MaxResident) {
        upload(entry);
        ++uploaded;
      }
    }
    entry.pending.reset();
    entry.job = {};
  }
}

void ThumbnailCache::update() {
  SONNET_ZONE();
  ++m_frame;
  drain(UploadsPerFrame);
  for (auto it = m_entries.begin(); it != m_entries.end();) {
    auto &entry = it->second;
    if (!entry.pending && m_frame - entry.used > IdleFrames) {
      release(entry);
      it = m_entries.erase(it);
    } else {
      ++it;
    }
  }
}

void ThumbnailCache::invalidate(const core::Uuid &uuid) {
  const auto found = m_entries.find(uuid);
  if (found == m_entries.end()) {
    return;
  }
  if (found->second.job.valid()) {
    m_assets.jobs().wait(found->second.job);
  }
  release(found->second);
  m_entries.erase(found);
}

void ThumbnailCache::settle() {
  for (auto &[uuid, entry] : m_entries) {
    if (entry.job.valid()) {
      m_assets.jobs().wait(entry.job);
    }
  }
  drain(m_entries.size());
}

bool ThumbnailCache::busy() const noexcept {
  return std::ranges::any_of(m_entries, [](const auto &entry) { return entry.second.pending != nullptr; });
}

} // namespace sonnet::editor
