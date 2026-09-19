#pragma once

#include <sonnet/editor/CommandStack.h>
#include <sonnet/editor/Selection.h>

#include <sonnet/assets/AssetDatabase.h>
#include <sonnet/core/Uuid.h>

#include <optional>
#include <string>

namespace sonnet::editor {

// The drag-and-drop payload of an asset: its 16 identity bytes. The inspector's pickers and the
// hierarchy accept it.
constexpr const char *AssetDragPayload = "SONNET_ASSET";

// "name (Type)" for the inspector's pickers and the browser; "(none)" for nil, "(missing)" for
// an identity the database does not know.
[[nodiscard]] std::string assetLabel(const assets::AssetDatabase &assets, const core::Uuid &uuid);

// The project's assets by type and name (docs/editor.md, "Asset browser"): a filter, a type
// filter, one row per asset. Clicking inspects the asset; rows are drag sources; the buttons
// create a material file in the assets folder and a script in the scripts folder.
class AssetBrowserPanel {
public:
  AssetBrowserPanel(assets::AssetDatabase &assets, Selection &selection);

  void draw(bool &open);

  // The asset the inspector shows while no entity is selected; nil for none.
  [[nodiscard]] core::Uuid inspected() const noexcept {
    return m_inspected;
  }
  void inspect(core::Uuid uuid) {
    m_inspected = uuid;
  }

private:
  void createMaterial();
  void createScript();

  assets::AssetDatabase &m_assets;
  Selection &m_selection;
  core::Uuid m_inspected;
  std::string m_filter;
  std::optional<assets::AssetType> m_type;
};

} // namespace sonnet::editor
