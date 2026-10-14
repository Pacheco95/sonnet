#pragma once

#include <sonnet/editor/CommandStack.h>
#include <sonnet/editor/Selection.h>
#include <sonnet/editor/TypeFilter.h>

#include <sonnet/assets/AssetDatabase.h>
#include <sonnet/core/Uuid.h>
#include <sonnet/world/World.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace sonnet::editor {

// The drag-and-drop payload of an asset: its 16 identity bytes. The inspector's pickers and the
// hierarchy accept it.
constexpr const char *AssetDragPayload = "SONNET_ASSET";

// "name (Type)" for the inspector's pickers and the browser; "(none)" for nil, "(missing)" for
// an identity the database does not know.
[[nodiscard]] std::string assetLabel(const assets::AssetDatabase &assets, const core::Uuid &uuid);

// A file: URL for a directory, percent-encoded so a space or a non-ASCII byte survives the trip to the
// platform's opener; the row menu's Show in file manager hands it to SDL_OpenURL.
[[nodiscard]] std::string directoryUrl(const std::filesystem::path &directory);

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

// The project's assets and prefab files by type and name (docs/editor.md, "Asset browser"): a filter, a multi-select
// type filter, one row per asset or prefab, sorted by the column header clicked last (Name by default). Clicking an
// asset inspects it without touching the selection; rows are drag sources; the buttons create a material file in the
// assets folder and a script in the scripts folder. A right-click on a row copies the source path, shows its folder in
// the file manager, or opens a script in the external editor.
class AssetBrowserPanel {
public:
  AssetBrowserPanel(assets::AssetDatabase &assets, world::World &world, Selection &selection);

  void draw(bool &open);
  // Opens a file at a line in the external editor; the row menu's Open entry for scripts uses it.
  void setOpenHandler(std::function<void(const std::string &, int)> handler) {
    m_open = std::move(handler);
  }

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
};

} // namespace sonnet::editor
