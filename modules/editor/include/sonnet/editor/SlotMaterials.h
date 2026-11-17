#pragma once

#include <sonnet/assets/AssetDatabase.h>
#include <sonnet/core/Error.h>
#include <sonnet/core/Uuid.h>

#include <cstddef>
#include <filesystem>
#include <vector>

namespace sonnet::editor {

// What the inspector does to a MeshRenderer's `slotMaterials` (docs/editor.md, "Material slots"),
// as functions on values so they can be tested without a UI.

// `slots` with `material` in slot `slot`, nil entries filling the slots before it. Trailing nil
// entries are dropped, so clearing every slot leaves the empty list a file leaves out.
[[nodiscard]] std::vector<core::Uuid> setSlotMaterial(std::vector<core::Uuid> slots, std::size_t slot,
                                                      const core::Uuid &material);

// Where "Make editable copy" writes a glTF material: beside its glTF file, named after the material
// (characters a file name cannot hold become `_`, an empty name is `material`), and numbered as an
// import numbers a name that is taken: `Skin.material.json`, then `Skin 2.material.json`.
[[nodiscard]] std::filesystem::path editableCopyPath(const assets::AssetInfo &material);

// Writes a glTF material's authored values into a new `.material.json` at editableCopyPath and
// registers it, returning its identity. The copy reads the same textures, the glTF's images, by
// identity. Fails for anything but a glTF material, or when the file cannot be written.
[[nodiscard]] core::Result<core::Uuid> makeEditableCopy(assets::AssetDatabase &assets, const core::Uuid &material);

} // namespace sonnet::editor
