#pragma once

#include <sonnet/editor/CommandStack.h>

#include <sonnet/assets/Asset.h>
#include <sonnet/assets/AssetDatabase.h>
#include <sonnet/core/Uuid.h>

#include <memory>

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

} // namespace sonnet::editor
