#include <sonnet/editor/ThumbnailCache.h>

#include <sonnet/assets/Thumbnail.h>
#include <sonnet/core/Log.h>
#include <sonnet/core/Profile.h>

#include <algorithm>
#include <filesystem>

namespace sonnet::editor {

ThumbnailCache::ThumbnailCache(rhi::IDevice &device, ui::ImGuiLayer &imgui, renderer::Renderer &renderer,
                               assets::AssetDatabase &assets)
    : m_device(device), m_imgui(imgui), m_assets(assets), m_materials(device, renderer, assets) {
}

ThumbnailCache::~ThumbnailCache() {
  for (const Retired &retired : m_retired) {
    m_imgui.unregisterImage(retired.texture);
    m_device.destroyImage(retired.image);
  }
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
      entry.request = *request;
      entry.ticket = ++m_tickets;
      entry.job = m_assets.jobs().schedule("thumbnail", [request = *request, pending = entry.pending] {
        if (request.type == assets::AssetType::Material) {
          if (auto cached = assets::cachedThumbnail(request)) {
            pending->picture = std::move(*cached);
          } else {
            pending->render = true; // the main thread hands it to the renderer
          }
        } else if (auto made = assets::makeThumbnail(request)) {
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

void ThumbnailCache::retire(Entry &entry) {
  if (entry.texture != 0) {
    m_retired.push_back({entry.image, entry.texture, m_frame + rhi::FramesInFlight + 1});
    entry = {};
    return;
  }
  release(entry);
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
    if (entry.pending->render) {
      // The worker is finished with the entry; the render and then the store take it from here.
      entry.pending->render = false;
      entry.pending->done.store(false, std::memory_order_relaxed);
      entry.job = {};
      entry.rendering = true;
      m_materials.request(uuid, entry.ticket);
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
  std::erase_if(m_retired, [&](const Retired &retired) {
    if (m_frame < retired.frame) {
      return false;
    }
    m_imgui.unregisterImage(retired.texture);
    m_device.destroyImage(retired.image);
    --m_resident;
    return true;
  });
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

void ThumbnailCache::render(renderer::RenderGraph &graph) {
  SONNET_ZONE();
  for (auto &result : m_materials.render(graph)) {
    const auto found = m_entries.find(result.uuid);
    if (found == m_entries.end() || found->second.ticket != result.ticket || !found->second.rendering) {
      continue; // invalidated since it was asked for
    }
    Entry &entry = found->second;
    entry.rendering = false;
    if (!result.picture) {
      SONNET_LOG_WARN("{}: no thumbnail: the material did not load", entry.request->source.string());
      entry.pending->done.store(true, std::memory_order_release);
      continue;
    }
    entry.pending->picture = std::move(result.picture);
    ++m_rendered;
    entry.job = m_assets.jobs().schedule("thumbnail store", [request = *entry.request, pending = entry.pending] {
      if (const auto stored = assets::storeThumbnail(request, *pending->picture); !stored) {
        SONNET_LOG_WARN("{}: thumbnail not cached: {}", request.source.string(), stored.error().toString());
      }
      pending->done.store(true, std::memory_order_release);
    });
  }
}

void ThumbnailCache::invalidate(const core::Uuid &uuid) {
  if (const auto found = m_entries.find(uuid); found != m_entries.end()) {
    if (found->second.job.valid()) {
      m_assets.jobs().wait(found->second.job);
    }
    retire(found->second);
    m_entries.erase(found);
  }
  m_materials.cancel(uuid);
  // A material edited in the editor has not touched its file, so the entry's age is no guide to it.
  std::error_code error;
  std::filesystem::remove(assets::thumbnailFile(m_assets.cacheDirectory(), uuid), error);
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
