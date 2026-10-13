#pragma once

#include <sonnet/editor/CommandStack.h>
#include <sonnet/editor/Selection.h>
#include <sonnet/editor/TypeFilter.h>

#include <sonnet/assets/AssetDatabase.h>
#include <sonnet/core/Uuid.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

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

// The project's assets by type and name (docs/editor.md, "Asset browser"): a filter, a multi-select type
// filter, one row per asset. Clicking inspects the asset without touching the selection; rows are drag sources; the
// buttons create a material file in the assets folder and a script in the scripts folder. A right-click on a row
// copies the source path, shows its folder in the file manager, or opens a script in the external editor.
class AssetBrowserPanel {
public:
  AssetBrowserPanel(assets::AssetDatabase &assets, Selection &selection);

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
  void drawContextMenu(const assets::AssetInfo &info);
  void createMaterial();
  void createScript();

  assets::AssetDatabase &m_assets;
  Selection &m_selection;
  core::Uuid m_inspected;
  std::uint64_t m_inspectedRevision = 0;
  std::string m_filter;
  TypeFilter m_types;
  std::function<void(const std::string &, int)> m_open;
};

} // namespace sonnet::editor
