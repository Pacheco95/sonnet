#pragma once

#include <sonnet/editor/CommandStack.h>

#include <sonnet/assets/Asset.h>
#include <sonnet/assets/AssetDatabase.h>
#include <sonnet/core/Error.h>
#include <sonnet/core/Uuid.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <span>

namespace sonnet::editor {

// Asset edits as commands (docs/editor.md, "Undo and redo"): they act on the database rather
// than the world, and hold their values before and after like the component commands.

// Sets a material's authored values; the file is written by the inspector's save, not here.
[[nodiscard]] std::unique_ptr<ICommand> materialEditCommand(assets::AssetDatabase &assets, core::Uuid material,
                                                            assets::MaterialSource before,
                                                            assets::MaterialSource after);
// Sets a texture's import settings, which rewrites the sidecar and re-imports.
[[nodiscard]] std::unique_ptr<ICommand> textureSettingsCommand(assets::AssetDatabase &assets, core::Uuid texture,
                                                               assets::TextureSettings before,
                                                               assets::TextureSettings after);
// Sets a glTF model's import settings, which rewrites the sidecar and re-imports the hierarchy.
[[nodiscard]] std::unique_ptr<ICommand> modelSettingsCommand(assets::AssetDatabase &assets, core::Uuid model,
                                                             assets::ModelSettings before, assets::ModelSettings after);

// Already applied commands, for CommandStack::record. The path callback follows moves in
// editor-owned scene tabs; it runs for undo and redo too.
using FilesMoved = std::function<void(const std::filesystem::path &, const std::filesystem::path &)>;
[[nodiscard]] core::Result<std::unique_ptr<ICommand>>
moveFilesCommand(assets::AssetDatabase &assets, world::World &world, const std::filesystem::path &from,
                 const std::filesystem::path &to, FilesMoved moved = {});
[[nodiscard]] core::Result<std::unique_ptr<ICommand>>
deleteFilesCommand(assets::AssetDatabase &assets, world::World &world, std::span<const std::filesystem::path> files);

// Copies files into the project and registers them, which is what the Assets panel's Import and a
// drop of files on it do (docs/editor.md, "Importing assets"). Each file goes into the project's
// first asset root, and a glTF file into a folder of its own named after it, with the buffers and
// images its URIs name at the same relative paths. A name already taken gets a number, as Save as
// prefab does: `wood 2.png`, the folder `crate 2`. A file no importer reads is logged and skipped.
// A model is also placed in `world` as a prefab, as opening the project does, so it can be
// instanced; undo leaves the prefab loaded.
// The result is the whole import as one command, already applied, for CommandStack::record: undo
// unregisters what it added and deletes the copies, their sidecars and the folders it made; redo
// copies the same files again and puts the sidecars back, so the identities stay. Fails, leaving
// nothing behind, when no file could be imported or a copy failed.
[[nodiscard]] core::Result<std::unique_ptr<ICommand>> importAssets(assets::AssetDatabase &assets, world::World &world,
                                                                   std::span<const std::filesystem::path> files);

} // namespace sonnet::editor
