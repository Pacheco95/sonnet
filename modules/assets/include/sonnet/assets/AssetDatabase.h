#pragma once

#include <sonnet/assets/Asset.h>
#include <sonnet/assets/Importers.h>

#include <sonnet/core/Error.h>
#include <sonnet/core/Uuid.h>
#include <sonnet/renderer/Renderer.h>

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
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
  explicit AssetDatabase(renderer::Renderer &renderer);
  ~AssetDatabase();
  AssetDatabase(const AssetDatabase &) = delete;
  AssetDatabase &operator=(const AssetDatabase &) = delete;

  // Scans `roots` under `projectRoot`, writes missing sidecars and registers what it finds; the
  // assets of a previous project are unloaded first. Problems are logged and skip the file.
  void open(const std::filesystem::path &projectRoot, std::span<const std::string> roots);
  void close();
  [[nodiscard]] bool isOpen() const noexcept {
    return !m_projectRoot.empty();
  }
  [[nodiscard]] const std::filesystem::path &projectRoot() const noexcept {
    return m_projectRoot;
  }

  [[nodiscard]] const AssetInfo *find(const core::Uuid &uuid) const;
  [[nodiscard]] const AssetInfo *findByPath(const std::filesystem::path &source) const;
  // Every asset, or those of one type, sorted by name then identity.
  [[nodiscard]] std::vector<const AssetInfo *> assets(std::optional<AssetType> type = std::nullopt) const;

  [[nodiscard]] renderer::MeshHandle mesh(const core::Uuid &uuid);
  [[nodiscard]] renderer::TextureHandle texture(const core::Uuid &uuid);
  [[nodiscard]] renderer::MaterialHandle material(const core::Uuid &uuid);
  [[nodiscard]] renderer::EnvironmentHandle environment(const core::Uuid &uuid);
  [[nodiscard]] const Model *model(const core::Uuid &uuid);

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
  [[nodiscard]] core::Result<nlohmann::json> readSidecar(const std::filesystem::path &sidecar) const;
  [[nodiscard]] core::Result<void> writeSidecar(const core::Uuid &uuid);
  void registerGltfSubAssets(const core::Uuid &uuid, const nlohmann::json &subAssets);
  [[nodiscard]] core::Result<nlohmann::json> listGltfSubAssets(const core::Uuid &uuid,
                                                               const std::filesystem::path &file);
  void unloadFile(const core::Uuid &uuid);
  void unloadTexture(const core::Uuid &uuid);
  [[nodiscard]] bool loadGltf(const core::Uuid &uuid);
  [[nodiscard]] renderer::TextureHandle loadFileTexture(const core::Uuid &uuid, const AssetInfo &info);
  [[nodiscard]] renderer::TextureHandle uploadTexture(const core::Uuid &uuid, const renderer::TextureData &data,
                                                      const std::string &name);
  [[nodiscard]] renderer::MaterialDesc resolve(const MaterialSource &source);
  void refreshMaterials(const core::Uuid &texture);

  renderer::Renderer &m_renderer;
  std::filesystem::path m_projectRoot;
  std::vector<std::string> m_roots;
  std::map<core::Uuid, AssetInfo> m_assets;
  std::unordered_map<core::Uuid, FileRecord> m_files;
  std::unordered_map<core::Uuid, renderer::MeshHandle> m_meshes;
  std::unordered_map<core::Uuid, LoadedTexture> m_textures;
  std::unordered_map<core::Uuid, LoadedMaterial> m_materials;
  std::unordered_map<core::Uuid, renderer::EnvironmentHandle> m_environments;
  std::unordered_map<core::Uuid, Model> m_models;
  std::unordered_map<core::Uuid, bool> m_gltfLoaded; // a glTF file's sub-assets are loaded together
  std::unordered_map<core::Uuid, bool> m_failed;     // loads that failed, not retried until reimport
  std::chrono::steady_clock::time_point m_lastPoll{};
};

} // namespace sonnet::assets
