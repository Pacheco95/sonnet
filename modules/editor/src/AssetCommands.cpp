#include <sonnet/editor/AssetCommands.h>

#include <sonnet/core/File.h>
#include <sonnet/core/Log.h>
#include <sonnet/world/Scene.h>

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace sonnet::editor {

namespace {

class MaterialEditCommand final : public ICommand {
public:
  MaterialEditCommand(assets::AssetDatabase &assets, core::Uuid material, assets::MaterialSource before,
                      assets::MaterialSource after)
      : m_assets(assets), m_material(material), m_before(before), m_after(after) {
    const assets::AssetInfo *info = m_assets.find(material);
    m_description = std::format("edit material {}", info != nullptr ? info->name : material.toString());
  }

  void apply(world::World &) override {
    m_assets.setMaterialSource(m_material, m_after);
  }
  void revert(world::World &) override {
    m_assets.setMaterialSource(m_material, m_before);
  }
  std::string_view description() const override {
    return m_description;
  }
  bool isSceneEdit() const override {
    return false;
  }

private:
  assets::AssetDatabase &m_assets;
  core::Uuid m_material;
  assets::MaterialSource m_before;
  assets::MaterialSource m_after;
  std::string m_description;
};

class TextureSettingsCommand final : public ICommand {
public:
  TextureSettingsCommand(assets::AssetDatabase &assets, core::Uuid texture, assets::TextureSettings before,
                         assets::TextureSettings after)
      : m_assets(assets), m_texture(texture), m_before(before), m_after(after) {
    const assets::AssetInfo *info = m_assets.find(texture);
    m_description = std::format("import settings of {}", info != nullptr ? info->name : texture.toString());
  }

  void apply(world::World &) override {
    set(m_after);
  }
  void revert(world::World &) override {
    set(m_before);
  }
  std::string_view description() const override {
    return m_description;
  }
  bool isSceneEdit() const override {
    return false;
  }

private:
  void set(const assets::TextureSettings &settings) {
    if (const auto result = m_assets.setTextureSettings(m_texture, settings); !result) {
      SONNET_LOG_ERROR("{}", result.error().toString());
    }
  }

  assets::AssetDatabase &m_assets;
  core::Uuid m_texture;
  assets::TextureSettings m_before;
  assets::TextureSettings m_after;
  std::string m_description;
};

struct ImportCopy {
  std::filesystem::path from;
  std::filesystem::path to;
};

std::filesystem::path sidecarOf(const std::filesystem::path &file) {
  return std::filesystem::path{file} += ".meta";
}

// The copies of an import and the folders made for them, parents first. Copying registers every
// copy an importer reads, the images beside a glTF file included, as opening the project would.
class ImportAssetsCommand final : public ICommand {
public:
  ImportAssetsCommand(assets::AssetDatabase &assets, std::vector<ImportCopy> copies,
                      std::vector<std::filesystem::path> directories, std::string description)
      : m_assets(assets), m_copies(std::move(copies)), m_directories(std::move(directories)),
        m_description(std::move(description)) {
  }

  [[nodiscard]] core::Result<void> copy(world::World &world) {
    for (const std::filesystem::path &directory : m_directories) {
      if (std::error_code error; !std::filesystem::create_directory(directory, error) && error) {
        removeCopies(0);
        return std::unexpected(
            core::Error{std::format("{}: {}", directory.string(), error.message()), core::ErrorCategory::Io});
      }
    }
    for (std::size_t i = 0; i < m_copies.size(); ++i) {
      // Never over a file: a name that was free when the import was planned may not be any more.
      if (std::error_code error; !std::filesystem::copy_file(m_copies[i].from, m_copies[i].to, error) || error) {
        removeCopies(i);
        return std::unexpected(
            core::Error{std::format("copying {} to {}: {}", m_copies[i].from.string(), m_copies[i].to.string(),
                                    error ? error.message() : "the file exists"),
                        core::ErrorCategory::Io});
      }
    }
    for (const auto &[sidecar, text] : m_sidecars) {
      if (const auto written = core::writeFile(sidecar, text); !written) {
        SONNET_LOG_ERROR("{}", written.error().toString());
      }
    }
    for (const ImportCopy &copy : m_copies) {
      if (!assets::AssetDatabase::sourceType(copy.to)) {
        continue;
      }
      const auto registered = m_assets.registerFile(copy.to);
      if (!registered) {
        SONNET_LOG_ERROR("{}", registered.error().toString());
        continue;
      }
      // A model is placed as a prefab, as opening the project places every model, so it can be
      // dropped on the hierarchy. An undo leaves the prefab loaded, and a redo finds it there.
      const assets::AssetInfo *info = m_assets.find(*registered);
      if (info->type == assets::AssetType::Model && !world.find(*registered)) {
        if (const assets::Model *model = m_assets.model(*registered)) {
          static_cast<void>(world::loadModelPrefab(world, *model, *registered, info->name));
        }
      }
    }
    return {};
  }

  void apply(world::World &world) override {
    if (const auto copied = copy(world); !copied) {
      SONNET_LOG_ERROR("{}", copied.error().toString());
    }
  }
  void revert(world::World &) override {
    // The sidecars are kept for redo, which would otherwise give the copies new identities.
    m_sidecars.clear();
    for (const ImportCopy &copy : m_copies) {
      const assets::AssetInfo *info = m_assets.findByPath(copy.to);
      if (info == nullptr) {
        continue;
      }
      const std::filesystem::path sidecar = sidecarOf(copy.to);
      if (const auto bytes = core::readFile(sidecar)) {
        m_sidecars.emplace_back(sidecar, std::string{reinterpret_cast<const char *>(bytes->data()), bytes->size()});
      }
      m_assets.unregister(info->uuid);
    }
    removeCopies(m_copies.size());
  }
  std::string_view description() const override {
    return m_description;
  }
  bool isSceneEdit() const override {
    return false;
  }

private:
  // Deletes the first `count` copies with their sidecars, then the folders made for them, which
  // are left in place when something else has been put in them since.
  void removeCopies(std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
      std::error_code error;
      std::filesystem::remove(m_copies[i].to, error);
      std::filesystem::remove(sidecarOf(m_copies[i].to), error);
    }
    for (auto directory = m_directories.rbegin(); directory != m_directories.rend(); ++directory) {
      std::error_code error;
      std::filesystem::remove(*directory, error);
    }
  }

  assets::AssetDatabase &m_assets;
  std::vector<ImportCopy> m_copies;
  std::vector<std::filesystem::path> m_directories;
  std::vector<std::pair<std::filesystem::path, std::string>> m_sidecars;
  std::string m_description;
};

// `name` in `directory`, or with " 2", " 3" and so on after its stem until it names nothing on
// disk and nothing claimed earlier by the same import. A material's stem stops before
// `.material.json`, which the database recognises it by.
std::filesystem::path freeName(const std::filesystem::path &directory, const std::filesystem::path &name,
                               const std::vector<std::filesystem::path> &claimed) {
  const std::u8string filename = name.u8string();
  const std::u8string_view material = u8".material.json";
  const bool isMaterial = filename.size() > material.size() && filename.ends_with(material);
  const std::filesystem::path stem =
      isMaterial ? std::filesystem::path{filename.substr(0, filename.size() - material.size())} : name.stem();
  const std::filesystem::path extension = isMaterial ? std::filesystem::path{material} : name.extension();
  std::filesystem::path candidate = directory / name;
  for (int n = 2; std::filesystem::exists(candidate) || std::ranges::contains(claimed, candidate); ++n) {
    candidate = directory / (std::filesystem::path{stem} += std::format(" {}", n)) += extension;
  }
  return candidate;
}

} // namespace

core::Result<std::unique_ptr<ICommand>> importAssets(assets::AssetDatabase &assets, world::World &world,
                                                     std::span<const std::filesystem::path> files) {
  if (!assets.isOpen() || assets.bundle() != nullptr || assets.roots().empty()) {
    return std::unexpected(core::Error{"importing needs an open project with an asset root", core::ErrorCategory::Io});
  }
  const std::filesystem::path root = assets.projectRoot() / assets.roots().front();
  std::vector<ImportCopy> copies;
  std::vector<std::filesystem::path> claimed; // every destination, files and folders
  std::vector<std::filesystem::path> directories;
  std::filesystem::path last;
  // The folders a destination needs that do not exist, parents first.
  const auto addDirectories = [&](const std::filesystem::path &file) {
    std::vector<std::filesystem::path> missing;
    for (std::filesystem::path directory = file.parent_path();
         !directory.empty() && !std::filesystem::exists(directory) && !std::ranges::contains(directories, directory);
         directory = directory.parent_path()) {
      missing.push_back(directory);
    }
    directories.insert(directories.end(), missing.rbegin(), missing.rend());
  };
  for (const std::filesystem::path &file : files) {
    const std::optional<assets::AssetType> type = assets::AssetDatabase::sourceType(file);
    if (std::error_code error; !std::filesystem::is_regular_file(file, error) || !type) {
      SONNET_LOG_ERROR("{}: not a file an importer reads, skipped", file.string());
      continue;
    }
    if (*type != assets::AssetType::Model) {
      const std::filesystem::path to = freeName(root, file.filename(), claimed);
      claimed.push_back(to);
      addDirectories(to);
      copies.push_back({.from = file, .to = to});
      last = file.filename();
      continue;
    }
    const auto dependencies = assets::gltfDependencies(file);
    if (!dependencies) {
      SONNET_LOG_ERROR("{}, skipped", dependencies.error().toString());
      continue;
    }
    const std::filesystem::path folder = freeName(root, file.stem(), claimed);
    claimed.push_back(folder);
    const std::size_t first = copies.size();
    copies.push_back({.from = file, .to = folder / file.filename()});
    for (const std::filesystem::path &dependency : *dependencies) {
      copies.push_back({.from = file.parent_path() / dependency, .to = folder / dependency});
    }
    for (std::size_t i = first; i < copies.size(); ++i) {
      addDirectories(copies[i].to);
    }
    last = file.filename();
  }
  if (copies.empty()) {
    return std::unexpected(core::Error{"nothing to import", core::ErrorCategory::Io});
  }
  const auto imported = std::ranges::count_if(
      files, [&](const std::filesystem::path &file) { return std::ranges::contains(copies, file, &ImportCopy::from); });
  std::string description =
      imported == 1 ? std::format("import {}", last.string()) : std::format("import {} files", imported);
  auto command =
      std::make_unique<ImportAssetsCommand>(assets, std::move(copies), std::move(directories), std::move(description));
  if (auto copied = command->copy(world); !copied) {
    return std::unexpected(copied.error());
  }
  return command;
}

std::unique_ptr<ICommand> materialEditCommand(assets::AssetDatabase &assets, core::Uuid material,
                                              assets::MaterialSource before, assets::MaterialSource after) {
  return std::make_unique<MaterialEditCommand>(assets, material, before, after);
}

std::unique_ptr<ICommand> textureSettingsCommand(assets::AssetDatabase &assets, core::Uuid texture,
                                                 assets::TextureSettings before, assets::TextureSettings after) {
  return std::make_unique<TextureSettingsCommand>(assets, texture, before, after);
}

} // namespace sonnet::editor
