#pragma once

#include <sonnet/editor/CommandStack.h>
#include <sonnet/editor/Selection.h>
#include <sonnet/editor/TypeFilter.h>

#include <sonnet/assets/AssetDatabase.h>
#include <sonnet/core/Math.h>
#include <sonnet/core/Uuid.h>
#include <sonnet/world/World.h>

#include <imgui.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

// A listed row, computed once per frame: an imported asset, prefab, scene, folder or plain file.
struct AssetRow {
  const assets::AssetInfo *info = nullptr;
  core::Uuid uuid;
  std::string name;
  std::string_view type;
  std::string source;
  // What the row menu copies and shows in the file manager; empty for a built-in asset.
  std::filesystem::path file;
  bool directory{false};
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

// Disk-backed project browser (docs/editor.md, "Asset browser"): folder navigation, recursive
// name search, type filters and source file commands. Asset selection leaves entity selection alone.
class AssetBrowserPanel {
public:
  AssetBrowserPanel(assets::AssetDatabase &assets, world::World &world, Selection &selection);

  ~AssetBrowserPanel();
  void draw(bool &open);
  [[nodiscard]] const std::filesystem::path &currentFolder();
  void navigate(const std::filesystem::path &folder);
  void back();
  void forward();
  void up();
  [[nodiscard]] std::filesystem::path importFolder();
  [[nodiscard]] std::vector<AssetRow> rows();
  void setRenameHandler(
      std::function<core::Result<void>(std::span<const std::pair<std::filesystem::path, std::filesystem::path>>)>
          handler) {
    m_moveMany = std::move(handler);
  }
  void setSceneHandler(std::function<void(const std::filesystem::path &)> handler) {
    m_scene = std::move(handler);
  }
  void setCreateFolderHandler(std::function<core::Result<void>(const std::filesystem::path &)> handler) {
    m_createFolder = std::move(handler);
  }

  [[nodiscard]] bool focused() const noexcept;
  void
  setFileHandlers(std::function<core::Result<void>(const std::filesystem::path &, const std::filesystem::path &)> move,
                  std::function<core::Result<void>(std::span<const std::filesystem::path>)> remove,
                  std::function<AssetReferences(std::span<const core::Uuid>)> references) {
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
  void syncProject();
  void drawNavigation();
  void createFolder();
  void drawContextMenu(const AssetRow &row);
  void beginRename(const AssetRow &row);
  void confirmDelete(const AssetRow &row);
  void drawDeleteModal();
  void drawRenameModal();
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
  std::function<core::Result<void>(std::span<const std::filesystem::path>)> m_remove;
  std::function<AssetReferences(std::span<const core::Uuid>)> m_references;
  std::filesystem::path m_project;
  std::filesystem::path m_folder;
  std::vector<std::filesystem::path> m_history;
  std::size_t m_historyIndex{0};
  std::map<std::string, std::string> m_folders;
  bool m_settingsInstalled{false};
  ImGuiSelectionBasicStorage m_selected;
  std::vector<AssetRow> m_rows;
  std::function<void(const std::filesystem::path &)> m_scene;
  std::function<core::Result<void>(const std::filesystem::path &)> m_createFolder;
  std::function<core::Result<void>(std::span<const std::pair<std::filesystem::path, std::filesystem::path>>)>
      m_moveMany;
  std::vector<AssetRow> m_renameRows;
  std::vector<std::string> m_renameNames;
  bool m_batchRenameRequested{false};
  std::vector<std::filesystem::path> m_deleteFiles;
  std::vector<core::Uuid> m_deleteIds;
  ImGuiID m_focused{0};
  ImGuiID m_renaming{0};
  std::string m_rename;
  bool m_renameFocus{false};
  std::optional<std::pair<std::filesystem::path, std::filesystem::path>> m_pendingRename;
  AssetReferences m_deleteReferences;
  bool m_deleteRequested{false};
  std::string m_fileError;
  int m_drawnFrame = -1; // ImGui's frame count when the panel last showed a project
  glm::vec2 m_min{0.0f};
  glm::vec2 m_max{0.0f};
};

} // namespace sonnet::editor
