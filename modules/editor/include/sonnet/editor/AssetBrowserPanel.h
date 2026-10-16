#pragma once

#include <sonnet/editor/CommandStack.h>
#include <sonnet/editor/Selection.h>
#include <sonnet/editor/TypeFilter.h>

#include <sonnet/assets/AssetDatabase.h>
#include <sonnet/core/Math.h>
#include <sonnet/core/Uuid.h>
#include <sonnet/world/World.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace sonnet::editor {

// The drag-and-drop payload of an asset: its 16 identity bytes. The inspector's pickers and the
// hierarchy accept it.
constexpr const char *AssetDragPayload = "SONNET_ASSET";

// "name (Type)" for the inspector's pickers and the browser; "(none)" for nil, "(missing)" for
// an identity the database does not know.
[[nodiscard]] std::string assetLabel(const assets::AssetDatabase &assets, const core::Uuid &uuid);

// What the browser's Source column shows: the file relative to the project, "/ name" added for a sub-asset,
// "built-in" for an asset without a file.
[[nodiscard]] std::string assetSourceLabel(const assets::AssetInfo &info, const std::filesystem::path &projectRoot);

// A listed row, computed once per frame for drawing and sorting: a database asset, or a prefab loaded from a file,
// which has no AssetInfo.
struct AssetRow {
  const assets::AssetInfo *info = nullptr;
  core::Uuid uuid;
  std::string name;
  std::string_view type;
  std::string source;
  // What the row menu copies and shows in the file manager; empty for a built-in asset.
  std::filesystem::path file;
};
[[nodiscard]] AssetRow assetRow(const assets::AssetInfo &info, const std::filesystem::path &projectRoot);
// A prefab's row: type "Prefab", its file relative to the project as the Source.
[[nodiscard]] AssetRow prefabRow(const core::Uuid &uuid, std::string name, const std::filesystem::path &file,
                                 const std::filesystem::path &projectRoot);

enum class AssetSortColumn {
  Name,
  Type,
  Source
};

// Orders the rows by a column of the browser: Name and Source ignoring case, Type by the type's name, `descending`
// reversing that key; ties go by name ascending, then identity, so the order is total.
void sortAssetRows(std::span<AssetRow> rows, AssetSortColumn column, bool descending);

struct AssetReferences {
  std::size_t entities{0};
  std::size_t materials{0};
  std::size_t instances{0};
};

// The project's assets and prefab files by type and name (docs/editor.md, "Asset browser"): a filter, a multi-select
// type filter, one row per asset or prefab, sorted by the column header clicked last (Name by default). Clicking an
// asset inspects it without touching the selection; rows are drag sources; the buttons create a material file in the
// assets folder and a script in the scripts folder, or import files, as does a drop of files on the panel. A
// right-click on a row copies the source path, selects it in the file manager, or opens a script in the external
// editor.
class AssetBrowserPanel {
public:
  AssetBrowserPanel(assets::AssetDatabase &assets, world::World &world, Selection &selection);

  void draw(bool &open);
  [[nodiscard]] bool focused() const noexcept;
  void
  setFileHandlers(std::function<core::Result<void>(const std::filesystem::path &, const std::filesystem::path &)> move,
                  std::function<core::Result<void>(const std::filesystem::path &)> remove,
                  std::function<AssetReferences(core::Uuid)> references) {
    m_move = std::move(move);
    m_remove = std::move(remove);
    m_references = std::move(references);
  }
  // Opens a file at a line in the external editor; the row menu's Open entry for scripts uses it.
  void setOpenHandler(std::function<void(const std::string &, int)> handler) {
    m_open = std::move(handler);
  }
  // What the Import button does; the editor opens a file chooser. The button is disabled without one.
  void setImportHandler(std::function<void()> handler) {
    m_import = std::move(handler);
  }
  // Whether a point in ImGui's screen space is on the panel as the last frame drew it with a project open, which is
  // where a drop of files is an import.
  [[nodiscard]] bool dropTarget(glm::vec2 screen) const;

  // The asset the inspector shows, in place of the selected entities, until the selection is next touched;
  // nil for none. Inspecting an asset leaves the selection (and the gizmo) as it is.
  [[nodiscard]] core::Uuid inspected() const noexcept {
    return m_selection.revision() == m_inspectedRevision ? m_inspected : core::Uuid{};
  }
  void inspect(core::Uuid uuid) {
    m_inspected = uuid;
    m_inspectedRevision = m_selection.revision();
  }

private:
  void drawContextMenu(const AssetRow &row);
  void beginRename(const AssetRow &row);
  void confirmDelete(const AssetRow &row);
  void drawDeleteModal();
  void createMaterial();
  void createScript();

  assets::AssetDatabase &m_assets;
  world::World &m_world;
  Selection &m_selection;
  core::Uuid m_inspected;
  std::uint64_t m_inspectedRevision = 0;
  std::string m_filter;
  TypeFilter m_types;
  AssetSortColumn m_sortColumn = AssetSortColumn::Name;
  bool m_sortDescending = false;
  std::function<void(const std::string &, int)> m_open;
  std::function<void()> m_import;
  std::function<core::Result<void>(const std::filesystem::path &, const std::filesystem::path &)> m_move;
  std::function<core::Result<void>(const std::filesystem::path &)> m_remove;
  std::function<AssetReferences(core::Uuid)> m_references;
  core::Uuid m_focused;
  core::Uuid m_renaming;
  std::string m_rename;
  bool m_renameFocus{false};
  std::optional<std::pair<std::filesystem::path, std::filesystem::path>> m_pendingRename;
  core::Uuid m_deleteUuid;
  std::filesystem::path m_delete;
  AssetReferences m_deleteReferences;
  bool m_deleteRequested{false};
  std::string m_fileError;
  int m_drawnFrame = -1; // ImGui's frame count when the panel last showed a project
  glm::vec2 m_min{0.0f};
  glm::vec2 m_max{0.0f};
};

} // namespace sonnet::editor
