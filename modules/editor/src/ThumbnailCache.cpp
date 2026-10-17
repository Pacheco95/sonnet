#include <sonnet/editor/ThumbnailCache.h>

#include <sonnet/assets/Thumbnail.h>
#include <sonnet/core/Log.h>
#include <sonnet/core/Profile.h>

#include <algorithm>
#include <filesystem>
#include <set>

namespace sonnet::editor {

ThumbnailCache::ThumbnailCache(rhi::IDevice &device, ui::ImGuiLayer &imgui, renderer::Renderer &renderer,
                               assets::AssetDatabase &assets, const world::World &world)
    : m_device(device), m_imgui(imgui), m_assets(assets), m_world(world), m_pictures(device, renderer, assets, world) {
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

namespace {

ThumbnailRenderer::Subject subjectOf(const assets::ThumbnailRequest &request) {
  if (request.prefab || request.type == assets::AssetType::Model) {
    return ThumbnailRenderer::Subject::Prefab;
  }
  return request.type == assets::AssetType::Mesh ? ThumbnailRenderer::Subject::Mesh
                                                 : ThumbnailRenderer::Subject::Material;
}

} // namespace

std::optional<assets::ThumbnailRequest> ThumbnailCache::requestFor(const core::Uuid &uuid) const {
  if (auto request = m_assets.thumbnailRequest(uuid)) {
    return request;
  }
  // A prefab is not an asset: its file is the source, and nothing but the files it is drawn from age it.
  const flecs::entity prefab = m_world.find(uuid);
  const auto *file = prefab ? prefab.try_get<world::PrefabFile>() : nullptr;
  if (file == nullptr) {
    return std::nullopt;
  }
  std::error_code error;
  assets::ThumbnailRequest request;
  request.uuid = uuid;
  request.type = assets::AssetType::Model;
  request.prefab = true;
  request.source = file->path;
  request.cache = m_assets.cacheDirectory();
  request.sourceTime = std::filesystem::last_write_time(file->path, error);
  // The files of the prefabs it nests, and of the meshes and materials it draws; a nested prefab's own parts are
  // copies in this tree, so one walk finds them all.
  std::set<std::filesystem::path> files;
  std::set<flecs::entity_t> seen;
  std::vector<flecs::entity> pending{prefab};
  while (!pending.empty()) {
    const flecs::entity entity = pending.back();
    pending.pop_back();
    if (!seen.insert(entity.id()).second) {
      continue;
    }
    if (entity != prefab) {
      if (const auto *nested = entity.try_get<world::PrefabFile>()) {
        files.insert(nested->path);
      }
    }
    if (const flecs::entity base = m_world.prefabOf(entity)) {
      pending.push_back(base);
    }
    if (const auto *renderer = entity.try_get<world::MeshRenderer>()) {
      for (const core::Uuid &asset : {renderer->mesh, renderer->material}) {
        if (const assets::AssetInfo *info = m_assets.find(asset); info != nullptr && info->source != "builtin") {
          files.insert(info->source);
          files.insert(info->source.string() + ".meta");
        }
      }
      request.store = request.store && !m_assets.hasUnsavedEdits(renderer->material);
    }
    for (const flecs::entity child : m_world.children(entity)) {
      pending.push_back(child);
    }
  }
  request.dependencies.assign(files.begin(), files.end());
  return request;
}

ThumbnailImage ThumbnailCache::get(const core::Uuid &uuid) {
  auto found = m_entries.find(uuid);
  if (found == m_entries.end()) {
    Entry entry;
    entry.used = m_frame;
    if (const auto request = requestFor(uuid)) {
      entry.pending = std::make_shared<Pending>();
      entry.request = *request;
      entry.ticket = ++m_tickets;
      entry.job = m_assets.jobs().schedule("thumbnail", [request = *request, pending = entry.pending] {
        if (assets::isRendered(request)) {
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
      // Held back while the renderer has enough to do: a folder of hundreds of models asks for them all at once, and
      // each one imports its meshes, which are uploaded on this thread.
      if (m_pictures.queued() >= ThumbnailRenderer::Backlog) {
        continue;
      }
      // The worker is finished with the entry; the render and then the store take it from here.
      entry.pending->render = false;
      entry.pending->done.store(false, std::memory_order_relaxed);
      entry.job = {};
      entry.rendering = true;
      m_pictures.request(uuid, entry.ticket, subjectOf(*entry.request));
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
  for (auto &result : m_pictures.render(graph)) {
    const auto found = m_entries.find(result.uuid);
    if (found == m_entries.end() || found->second.ticket != result.ticket || !found->second.rendering) {
      continue; // invalidated since it was asked for
    }
    Entry &entry = found->second;
    entry.rendering = false;
    if (!result.picture) {
      SONNET_LOG_WARN("{}: no thumbnail: nothing to draw", entry.request->source.string());
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

void ThumbnailCache::drop(const core::Uuid &uuid) {
  if (const auto found = m_entries.find(uuid); found != m_entries.end()) {
    if (found->second.job.valid()) {
      m_assets.jobs().wait(found->second.job);
    }
    retire(found->second);
    m_entries.erase(found);
  }
  m_pictures.cancel(uuid);
}

void ThumbnailCache::invalidate(const core::Uuid &uuid) {
  // What is drawn from the same file, or lists it, is stale with it: the meshes of a model, and the prefabs that
  // draw them, nest the prefab or use the material.
  std::vector<core::Uuid> stale{uuid};
  if (const auto request = requestFor(uuid); request && !request->source.empty()) {
    for (const auto &[other, entry] : m_entries) {
      if (other != uuid && entry.request &&
          (entry.request->source == request->source ||
           std::ranges::contains(entry.request->dependencies, request->source))) {
        stale.push_back(other);
      }
    }
  }
  for (const core::Uuid &entry : stale) {
    drop(entry);
    // A material edited in the editor has not touched its file, so the entry's age is no guide to it.
    std::error_code error;
    std::filesystem::remove(assets::thumbnailFile(m_assets.cacheDirectory(), entry), error);
  }
}

void ThumbnailCache::invalidatePrefabs() {
  std::vector<core::Uuid> drawn;
  for (const auto &[uuid, entry] : m_entries) {
    if (entry.request && subjectOf(*entry.request) == ThumbnailRenderer::Subject::Prefab) {
      drawn.push_back(uuid);
    }
  }
  for (const core::Uuid &uuid : drawn) {
    drop(uuid);
    std::error_code error;
    std::filesystem::remove(assets::thumbnailFile(m_assets.cacheDirectory(), uuid), error);
  }
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
