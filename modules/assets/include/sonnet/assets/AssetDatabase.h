#pragma once

#include <sonnet/assets/Animation.h>
#include <sonnet/assets/Asset.h>
#include <sonnet/assets/Bundle.h>
#include <sonnet/assets/Importers.h>

#include <sonnet/core/Error.h>
#include <sonnet/core/JobSystem.h>
#include <sonnet/core/Uuid.h>
#include <sonnet/renderer/Renderer.h>

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace sonnet::assets {

// Maps identities to metadata and to loaded renderer objects (docs/assets.md, "Database"). A
// project's asset roots are scanned on open: every source file gets a `.meta` sidecar with its
// identity and import settings, and a glTF file's sub-assets are listed there too. Loading is
// on the main thread and on first use: asking for a mesh, texture, material or environment
// imports it, cooks textures into the project's cache and returns the renderer's handle, or an
// invalid handle when the import failed, which is logged. Hot reload re-imports a changed source
// and swaps the objects under the same identities.
class AssetDatabase {
public:
  // `jobs` carries the asynchronous requests below and has to outlive the database; a pool with
  // no workers imports on the thread that drains it, which is what the cook tool and the tests get.
  AssetDatabase(renderer::Renderer &renderer, core::JobSystem &jobs);
  ~AssetDatabase();
  AssetDatabase(const AssetDatabase &) = delete;
  AssetDatabase &operator=(const AssetDatabase &) = delete;

  // Scans `roots` under `projectRoot`, writes missing sidecars and registers what it finds; the
  // assets of a previous project are unloaded first. Problems are logged and skip the file.
  void open(const std::filesystem::path &projectRoot, std::span<const std::string> roots);
  // Opens a cooked bundle instead of a project folder (ADR-0011): its index becomes the same
  // asset list, and every loader below reads a payload instead of importing a source. There are
  // no sidecars, no re-import and no hot reload in this mode, so `pollChanges` finds nothing.
  // The built-in primitives are registered either way.
  [[nodiscard]] core::Result<void> openBundle(const std::filesystem::path &file);
  void close();
  [[nodiscard]] bool isOpen() const noexcept {
    return !m_projectRoot.empty() || m_bundle.has_value();
  }
  // The open bundle, for the scenes and prefabs it holds beside the assets; null in project mode.
  [[nodiscard]] const Bundle *bundle() const noexcept {
    return m_bundle ? &*m_bundle : nullptr;
  }
  [[nodiscard]] const std::filesystem::path &projectRoot() const noexcept {
    return m_projectRoot;
  }
  // The asset roots the project was opened with, relative to its root.
  [[nodiscard]] std::span<const std::string> roots() const noexcept {
    return m_roots;
  }

  [[nodiscard]] const AssetInfo *find(const core::Uuid &uuid) const;
  [[nodiscard]] const AssetInfo *findByPath(const std::filesystem::path &source) const;
  // Every asset, or those of one type, sorted by name then identity.
  [[nodiscard]] std::vector<const AssetInfo *> assets(std::optional<AssetType> type = std::nullopt) const;

  [[nodiscard]] renderer::MeshHandle mesh(const core::Uuid &uuid);
  [[nodiscard]] renderer::TextureHandle texture(const core::Uuid &uuid);

  // The asynchronous form of the two loaders above (ADR-0013). A loaded asset comes back at once;
  // anything else returns an invalid handle and schedules the import on the pool, so the caller
  // draws nothing for it this frame and asks again on the next. The import runs on a worker and
  // the renderer objects are created by a main-thread job, since recording an upload is not
  // thread-safe; both are done by the time `core::JobSystem::runMainThreadJobs` returns. Asking
  // again while a request is in flight does not schedule it twice. A glTF file is one request for
  // the whole file, as the synchronous path loads one, so a mesh and its textures arrive together.
  [[nodiscard]] renderer::MeshHandle requestMesh(const core::Uuid &uuid);
  [[nodiscard]] renderer::TextureHandle requestTexture(const core::Uuid &uuid);
  // Whether any request is still in flight, which is what a loading screen waits on.
  [[nodiscard]] bool loading() const noexcept {
    return !m_pending.empty();
  }
  // Runs every request to completion, for a caller that wants the synchronous behaviour back.
  void waitForLoads();
  [[nodiscard]] renderer::MaterialHandle material(const core::Uuid &uuid);
  [[nodiscard]] renderer::EnvironmentHandle environment(const core::Uuid &uuid);
  // A model's node hierarchy. From a source glTF it is read from the file's JSON without
  // importing a mesh or an image, so placing a model's prefab costs a parse rather than an import;
  // its payloads come when something asks for them. The model and its nodes stay where they are
  // until the file is re-imported, including while the rest of the file loads.
  [[nodiscard]] const Model *model(const core::Uuid &uuid);
  // A mesh's vertices and indices on the CPU, kept once the mesh is loaded, for collision shapes.
  [[nodiscard]] const renderer::MeshData *meshData(const core::Uuid &uuid);
  // A script's source, read on first use and again when the file changes.
  [[nodiscard]] const ScriptSource *script(const core::Uuid &uuid);
  // A sound file's bytes, read on first use and again when the file changes.
  [[nodiscard]] const SoundSource *sound(const core::Uuid &uuid);
  // A glTF skin or animation clip, loaded with the rest of its file. The pointer is valid until
  // the file is re-imported; the revision says whether it was.
  [[nodiscard]] const Skin *skin(const core::Uuid &uuid);
  [[nodiscard]] const AnimationClip *animation(const core::Uuid &uuid);
  // The request forms, as requestMesh: a loaded skin or clip at once, otherwise null and the
  // file's import scheduled, for the systems that ask every frame.
  [[nodiscard]] const Skin *requestSkin(const core::Uuid &uuid);
  [[nodiscard]] const AnimationClip *requestAnimation(const core::Uuid &uuid);
  // A new .lua file under the project, registered at once.
  [[nodiscard]] core::Result<core::Uuid> createScript(const std::filesystem::path &file, std::string_view code);

  // A material's authored values; editing them updates the renderer's material at once, and
  // saving writes the .material.json of a file material.
  [[nodiscard]] const MaterialSource *materialSource(const core::Uuid &uuid);
  void setMaterialSource(const core::Uuid &uuid, const MaterialSource &source);
  [[nodiscard]] core::Result<void> saveMaterial(const core::Uuid &uuid);
  // A new .material.json under the project, registered at once.
  [[nodiscard]] core::Result<core::Uuid> createMaterial(const std::filesystem::path &file,
                                                        const MaterialSource &source);

  // A texture's import settings, from its sidecar; setting them rewrites the sidecar and
  // re-imports.
  [[nodiscard]] TextureSettings textureSettings(const core::Uuid &uuid) const;
  [[nodiscard]] core::Result<void> setTextureSettings(const core::Uuid &uuid, const TextureSettings &settings);

  // Re-imports one file asset, or the file behind a sub-asset, swapping every loaded object.
  [[nodiscard]] core::Result<void> reimport(const core::Uuid &uuid);
  // Once per frame: checks the sources' modification times every half second and re-imports
  // what changed. Returns the identities re-imported.
  std::vector<core::Uuid> pollChanges();

  [[nodiscard]] std::filesystem::path cacheDirectory() const {
    return m_projectRoot / ".sonnet" / "cache";
  }
  // The renderer the loaded objects belong to.
  [[nodiscard]] renderer::Renderer &renderer() noexcept {
    return m_renderer;
  }

private:
  struct FileRecord {
    std::filesystem::path sidecar;
    std::filesystem::file_time_type sourceTime; // what the change poll compares
    std::string sourceHash;                     // glTF files: what the sidecar's sub-asset list is keyed on
    nlohmann::json settings;                    // the sidecar's "settings"
  };
  struct LoadedTexture {
    renderer::TextureHandle handle;
  };
  struct LoadedMaterial {
    renderer::MaterialHandle handle;
    MaterialSource source;
  };

  void registerBuiltins();
  void scanFile(const std::filesystem::path &file);
  // A cooked payload, or a logged failure that is not retried. Bundle mode only.
  [[nodiscard]] std::optional<std::vector<std::byte>> bundlePayload(const core::Uuid &uuid);
  [[nodiscard]] core::Result<nlohmann::json> readSidecar(const std::filesystem::path &sidecar) const;
  [[nodiscard]] core::Result<void> writeSidecar(const core::Uuid &uuid);
  void registerGltfSubAssets(const core::Uuid &uuid, const nlohmann::json &subAssets);
  [[nodiscard]] core::Result<nlohmann::json> listGltfSubAssets(const core::Uuid &uuid,
                                                               const std::filesystem::path &file);
  void unloadFile(const core::Uuid &uuid);
  void unloadTexture(const core::Uuid &uuid);
  [[nodiscard]] bool loadGltf(const core::Uuid &uuid);
  [[nodiscard]] renderer::TextureHandle loadFileTexture(const core::Uuid &uuid, const AssetInfo &info);

  // What a glTF import needs from the database, copied on the main thread so the worker that runs
  // the import touches none of it.
  struct GltfRequest {
    core::Uuid uuid;
    std::filesystem::path source;
    std::filesystem::path cache;
    std::string name;
    std::filesystem::file_time_type sourceTime;
    bool blockCompression{false};
  };
  // The result of that import: everything decoded, nothing created. Images line up with
  // `import.images`; an image that failed to decode has no data.
  struct GltfLoad {
    std::optional<GltfImport> import;
    std::vector<core::Uuid> imageUuids;
    std::vector<std::optional<renderer::TextureData>> images;
  };
  // Pure: reads files and decodes, touches no member and no renderer, so it runs on any thread.
  [[nodiscard]] static GltfLoad importGltfFiles(const GltfRequest &request);
  // The other half, main thread only: creates the renderer objects and registers the sub-assets.
  bool publishGltf(const GltfRequest &request, GltfLoad &&load);
  [[nodiscard]] std::optional<GltfRequest> gltfRequest(const core::Uuid &uuid);
  // Schedules a glTF file's import for a request of one of its sub-assets, unless the file is
  // loaded, failed or importing already: the request is always the whole file's.
  void requestGltf(const core::Uuid &uuid);

  // The same split for a texture that is a file of its own rather than a glTF sub-asset.
  struct TextureRequest {
    core::Uuid uuid;
    std::filesystem::path source;
    std::filesystem::path sidecar;
    std::filesystem::path cache;
    std::string name;
    std::filesystem::file_time_type sourceTime;
    TextureSettings settings;
    bool blockCompression{false};
  };
  [[nodiscard]] static std::optional<renderer::TextureData> importFileTexture(const TextureRequest &request);
  [[nodiscard]] std::optional<TextureRequest> textureRequest(const core::Uuid &uuid, const AssetInfo &info);

  // A request in flight, keyed by the asset whose import it is: a glTF file, or a file texture.
  // Only the main thread touches this map; a worker sees nothing but its own captured request.
  struct PendingLoad {
    core::JobHandle job;
  };
  void schedule(const core::Uuid &uuid, std::function<void()> work);
  [[nodiscard]] renderer::TextureHandle uploadTexture(const core::Uuid &uuid, const renderer::TextureData &data,
                                                      const std::string &name);
  [[nodiscard]] renderer::MaterialDesc resolve(const MaterialSource &source);
  void refreshMaterials(const core::Uuid &texture);

  renderer::Renderer &m_renderer;
  core::JobSystem &m_jobs;
  std::unordered_map<core::Uuid, PendingLoad> m_pending;
  std::optional<Bundle> m_bundle; // set in bundle mode, in which m_files stays empty
  std::filesystem::path m_projectRoot;
  std::vector<std::string> m_roots;
  std::map<core::Uuid, AssetInfo> m_assets;
  std::unordered_map<core::Uuid, FileRecord> m_files;
  std::unordered_map<core::Uuid, renderer::MeshHandle> m_meshes;
  std::unordered_map<core::Uuid, renderer::MeshData> m_meshData;
  std::unordered_map<core::Uuid, ScriptSource> m_scripts;
  std::unordered_map<core::Uuid, SoundSource> m_sounds;
  std::unordered_map<core::Uuid, Skin> m_skins;
  std::unordered_map<core::Uuid, AnimationClip> m_animations;
  std::uint64_t m_revision{0}; // what the next loaded script, sound, skin or clip is stamped with
  std::unordered_map<core::Uuid, LoadedTexture> m_textures;
  std::unordered_map<core::Uuid, LoadedMaterial> m_materials;
  std::unordered_map<core::Uuid, renderer::EnvironmentHandle> m_environments;
  std::unordered_map<core::Uuid, Model> m_models;
  std::unordered_map<core::Uuid, bool> m_gltfLoaded; // a glTF file's sub-assets are loaded together
  std::unordered_map<core::Uuid, bool> m_failed;     // loads that failed, not retried until reimport
  std::chrono::steady_clock::time_point m_lastPoll{};
};

} // namespace sonnet::assets
