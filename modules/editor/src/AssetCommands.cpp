#include <sonnet/editor/AssetCommands.h>

#include <sonnet/assets/Project.h>
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

// Sets a file's import settings, which rewrites its sidecar and re-imports it. `Set` is the
// database member that does so for the settings type.
template <typename Settings, core::Result<void> (assets::AssetDatabase::*Set)(const core::Uuid &, const Settings &)>
class SettingsCommand final : public ICommand {
public:
  SettingsCommand(assets::AssetDatabase &assets, core::Uuid file, Settings before, Settings after)
      : m_assets(assets), m_file(file), m_before(before), m_after(after) {
    const assets::AssetInfo *info = m_assets.find(file);
    m_description = std::format("import settings of {}", info != nullptr ? info->name : file.toString());
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
  void set(const Settings &settings) {
    if (const auto result = (m_assets.*Set)(m_file, settings); !result) {
      SONNET_LOG_ERROR("{}", result.error().toString());
    }
  }

  assets::AssetDatabase &m_assets;
  core::Uuid m_file;
  Settings m_before;
  Settings m_after;
  std::string m_description;
};

using TextureSettingsCommand = SettingsCommand<assets::TextureSettings, &assets::AssetDatabase::setTextureSettings>;
using ModelSettingsCommand = SettingsCommand<assets::ModelSettings, &assets::AssetDatabase::setModelSettings>;

bool underPath(const std::filesystem::path &path, const std::filesystem::path &root) {
  const auto relative = path.lexically_relative(root);
  return !relative.empty() && *relative.begin() != "..";
}

void warnRequires(assets::AssetDatabase &assets, const std::filesystem::path &from) {
  for (const auto *info : assets.assets(assets::AssetType::Script)) {
    const auto *source = assets.script(info->uuid);
    if (source != nullptr && source->code.find("require") != std::string::npos &&
        (source->code.find(from.stem().string()) != std::string::npos ||
         source->code.find(from.filename().string()) != std::string::npos)) {
      SONNET_LOG_WARN("{}: require may refer to moved or deleted {}; update it manually", info->source.string(),
                      from.string());
    }
  }
}

class MoveFilesCommand final : public ICommand {
public:
  MoveFilesCommand(assets::AssetDatabase &assets, std::filesystem::path from, std::filesystem::path to,
                   FilesMoved moved)
      : m_assets(assets), m_from(std::move(from)), m_to(std::move(to)), m_moved(std::move(moved)),
        m_description(std::format("rename {}", m_from.filename().string())) {
  }

  core::Result<void> move(world::World &world, const std::filesystem::path &from, const std::filesystem::path &to) {
    if (const auto result = m_assets.move(from, to); !result) {
      return result;
    }
    if (auto project = assets::Project::open(m_assets.projectRoot())) {
      const auto start = project->resolve(project->startScene);
      if (underPath(start, from)) {
        project->startScene = project->relative(start == from ? to : to / start.lexically_relative(from));
        if (const auto saved = project->save(); !saved) {
          if (const auto rollback = m_assets.move(to, from); !rollback) {
            SONNET_LOG_ERROR("{}", rollback.error().toString());
          }
          return saved;
        }
      }
    }
    for (auto prefab : world.prefabs()) {
      if (const auto *file = prefab.try_get<world::PrefabFile>(); file != nullptr && underPath(file->path, from)) {
        prefab.set<world::PrefabFile>({file->path == from ? to : to / file->path.lexically_relative(from)});
      }
    }
    if (m_moved) {
      m_moved(from, to);
    }
    warnRequires(m_assets, from);
    return {};
  }
  void apply(world::World &world) override {
    if (const auto result = move(world, m_from, m_to); !result) {
      SONNET_LOG_ERROR("{}", result.error().toString());
    }
  }
  void revert(world::World &world) override {
    if (const auto result = move(world, m_to, m_from); !result) {
      SONNET_LOG_ERROR("{}", result.error().toString());
    }
  }
  std::string_view description() const override {
    return m_description;
  }
  bool isSceneEdit() const override {
    return false;
  }

private:
  assets::AssetDatabase &m_assets;
  std::filesystem::path m_from;
  std::filesystem::path m_to;
  FilesMoved m_moved;
  std::string m_description;
};

struct DeletedPath {
  std::filesystem::path source;
  std::filesystem::path trash;
};

class DeleteFilesCommand final : public ICommand {
public:
  DeleteFilesCommand(assets::AssetDatabase &assets, std::vector<DeletedPath> paths, std::filesystem::path trash)
      : m_assets(assets), m_paths(std::move(paths)), m_trash(std::move(trash)) {
  }
  ~DeleteFilesCommand() override {
    std::error_code error;
    if (m_committed) {
      std::filesystem::remove_all(m_trash, error);
    } else {
      std::filesystem::remove(m_trash, error);
    } // Keep any files a failed rollback could not restore.
    if (error) {
      SONNET_LOG_ERROR("removing trash {}: {}", m_trash.string(), error.message());
    }
  }
  core::Result<void> remove(world::World &world) try {
    std::error_code error;
    for (const auto &path : m_paths) {
      if (!std::filesystem::exists(path.source, error) || std::filesystem::exists(path.trash, error)) {
        return std::unexpected(
            core::Error{"delete source is missing or its trash is still occupied", core::ErrorCategory::Io});
      }
    }
    m_assets.waitForLoads();
    std::filesystem::create_directories(m_trash, error);
    if (error) {
      return std::unexpected(core::Error{error.message(), core::ErrorCategory::Io});
    }
    // Move everything before unregistering, so a failed rename can roll back without losing metadata.
    for (const auto &path : m_paths) {
      warnRequires(m_assets, path.source);
    }
    std::size_t count = 0;
    for (const auto &path : m_paths) {
      std::filesystem::rename(path.source, path.trash, error);
      if (error) {
        const auto message = error.message();
        while (count > 0) {
          --count;
          std::filesystem::rename(m_paths[count].trash, m_paths[count].source, error);
        }
        return std::unexpected(core::Error{message, core::ErrorCategory::Io});
      }
      ++count;
    }
    m_links.clear();
    const auto capture = [&](const auto &self, flecs::entity entity) -> void {
      if (const auto base = world.prefabOf(entity)) {
        bool deleted = false;
        for (auto root : world.prefabs()) {
          const auto *file = root.try_get<world::PrefabFile>();
          const auto *info = m_assets.find(world.uuidOf(root));
          const auto source = file != nullptr ? file->path : info != nullptr ? info->source : std::filesystem::path{};
          if (!source.empty() &&
              std::ranges::any_of(m_paths, [&](const auto &p) { return underPath(source, p.source); }) &&
              (base == root || world.isDescendant(base, root))) {
            deleted = true;
          }
        }
        if (deleted) {
          m_links.emplace_back(world.uuidOf(entity), world.uuidOf(base));
        }
      }
      for (auto child : world.children(entity)) {
        self(self, child);
      }
    };
    for (auto root : world.roots()) {
      capture(capture, root);
    }
    for (auto root : world.prefabs()) {
      capture(capture, root);
    }
    for (const auto &path : m_paths) {
      std::vector<core::Uuid> ids;
      for (const auto *info : m_assets.assets()) {
        if (info->parent.isNil() && info->source != "builtin" && underPath(info->source, path.source)) {
          ids.push_back(info->uuid);
        }
      }
      for (const auto &id : ids) {
        m_assets.unregister(id);
        if (const auto prefab = world.find(id); prefab && prefab.has(flecs::Prefab)) {
          world.destroyEntity(prefab);
        }
      }
      for (auto prefab : world.prefabs()) {
        const auto *file = prefab.try_get<world::PrefabFile>();
        if (file != nullptr && underPath(file->path, path.source)) {
          world.destroyEntity(prefab);
        }
      }
    }
    m_committed = true;
    return {};
  } catch (const std::filesystem::filesystem_error &error) {
    return std::unexpected(core::Error{error.what(), core::ErrorCategory::Io});
  }
  void apply(world::World &world) override {
    if (const auto result = remove(world); !result) {
      SONNET_LOG_ERROR("{}", result.error().toString());
    }
  }
  void revert(world::World &world) override try {
    std::error_code error;
    for (const auto &path : m_paths) {
      if (std::filesystem::exists(path.source, error)) {
        SONNET_LOG_ERROR("{}: cannot restore over an existing file", path.source.string());
        return;
      }
    }
    std::size_t count = 0;
    for (const auto &path : m_paths) {
      std::filesystem::rename(path.trash, path.source, error);
      if (error) {
        SONNET_LOG_ERROR("restoring {}: {}", path.source.string(), error.message());
        while (count > 0) {
          --count;
          std::filesystem::rename(m_paths[count].source, m_paths[count].trash, error);
        }
        return;
      }
      ++count;
    }
    std::vector<std::filesystem::path> files;
    for (const auto &path : m_paths) {
      if (std::filesystem::is_directory(path.source, error)) {
        for (const auto &entry : std::filesystem::recursive_directory_iterator{path.source, error}) {
          if (entry.is_regular_file()) {
            files.push_back(entry.path());
          }
        }
      } else {
        files.push_back(path.source);
      }
    }
    std::vector<std::filesystem::path> prefabs;
    for (const auto &file : files) {
      if (file.string().ends_with(".prefab.json")) {
        prefabs.push_back(file);
      }
      if (!assets::AssetDatabase::sourceType(file)) {
        continue;
      }
      if (auto id = m_assets.registerFile(file)) {
        const auto *info = m_assets.find(*id);
        if (info->type == assets::AssetType::Model && !world.find(*id)) {
          if (const auto *model = m_assets.model(*id)) {
            static_cast<void>(world::loadModelPrefab(world, *model, *id, info->name));
          }
        }
      } else {
        SONNET_LOG_ERROR("{}", id.error().toString());
      }
    }
    for (const auto &failure : world::loadPrefabFiles(world, prefabs)) {
      SONNET_LOG_ERROR("{}", failure.toString());
    }
    for (const auto &[entityId, baseId] : m_links) {
      const auto entity = world.find(entityId);
      const auto base = world.find(baseId);
      if (entity && base) {
        const auto children = world.children(entity);
        entity.add(flecs::IsA, base);
        // IsA instantiates children again. These instances already have their original
        // descendants and identities, so discard only the newly generated copies.
        for (auto child : world.children(entity)) {
          if (!std::ranges::contains(children, child)) {
            world.destroyEntity(child);
          }
        }
      }
    }
  } catch (const std::filesystem::filesystem_error &error) {
    SONNET_LOG_ERROR("restoring deleted files: {}", error.what());
  }
  std::string_view description() const override {
    return "delete files";
  }
  bool isSceneEdit() const override {
    return false;
  }

private:
  assets::AssetDatabase &m_assets;
  std::vector<DeletedPath> m_paths;
  std::vector<std::pair<core::Uuid, core::Uuid>> m_links;
  std::filesystem::path m_trash;
  bool m_committed{false};
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

core::Result<std::unique_ptr<ICommand>> moveFilesCommand(assets::AssetDatabase &assets, world::World &world,
                                                         const std::filesystem::path &from,
                                                         const std::filesystem::path &to, FilesMoved moved) {
  auto command = std::make_unique<MoveFilesCommand>(assets, from, to, std::move(moved));
  if (const auto result = command->move(world, from, to); !result) {
    return std::unexpected(result.error());
  }
  return command;
}

core::Result<std::unique_ptr<ICommand>> deleteFilesCommand(assets::AssetDatabase &assets, world::World &world,
                                                           std::span<const std::filesystem::path> files) try {
  if (!assets.isOpen() || assets.bundle() != nullptr || files.empty()) {
    return std::unexpected(core::Error{"deleting files needs an open project", core::ErrorCategory::Io});
  }
  std::error_code error;
  const auto root = std::filesystem::weakly_canonical(assets.projectRoot(), error);
  const auto trash = root / ".sonnet" / "trash" / core::Uuid::generate().toString();
  std::vector<DeletedPath> paths;
  for (const auto &file : files) {
    const auto source = std::filesystem::absolute(file, error).lexically_normal();
    if (error) {
      return std::unexpected(core::Error{error.message(), core::ErrorCategory::Io});
    }
    const auto resolved = std::filesystem::weakly_canonical(source, error);
    if (error || resolved == root || !underPath(resolved, root) || underPath(resolved, root / ".sonnet") ||
        !std::filesystem::exists(source, error) || resolved == root / "project.json") {
      return std::unexpected(core::Error{"invalid path for deletion", core::ErrorCategory::Io});
    }
    if (std::ranges::any_of(paths, [&](const auto &path) { return underPath(source, path.source); })) {
      continue;
    }
    std::erase_if(paths, [&](const auto &path) { return underPath(path.source, source); });
    paths.push_back({source, {}});
  }
  const auto originals = paths;
  for (const auto &path : originals) {
    if (!std::filesystem::is_directory(path.source) && std::filesystem::exists(sidecarOf(path.source)) &&
        !std::ranges::any_of(paths, [&](const auto &p) { return p.source == sidecarOf(path.source); })) {
      paths.push_back({sidecarOf(path.source), {}});
    }
  }
  for (std::size_t i = 0; i < paths.size(); ++i) {
    paths[i].trash = trash / std::to_string(i);
  }
  auto command = std::make_unique<DeleteFilesCommand>(assets, std::move(paths), trash);
  if (const auto result = command->remove(world); !result) {
    return std::unexpected(result.error());
  }
  return command;
} catch (const std::filesystem::filesystem_error &error) {
  return std::unexpected(core::Error{error.what(), core::ErrorCategory::Io});
}

std::unique_ptr<ICommand> materialEditCommand(assets::AssetDatabase &assets, core::Uuid material,
                                              assets::MaterialSource before, assets::MaterialSource after) {
  return std::make_unique<MaterialEditCommand>(assets, material, before, after);
}

std::unique_ptr<ICommand> textureSettingsCommand(assets::AssetDatabase &assets, core::Uuid texture,
                                                 assets::TextureSettings before, assets::TextureSettings after) {
  return std::make_unique<TextureSettingsCommand>(assets, texture, before, after);
}

std::unique_ptr<ICommand> modelSettingsCommand(assets::AssetDatabase &assets, core::Uuid model,
                                               assets::ModelSettings before, assets::ModelSettings after) {
  return std::make_unique<ModelSettingsCommand>(assets, model, before, after);
}

} // namespace sonnet::editor
