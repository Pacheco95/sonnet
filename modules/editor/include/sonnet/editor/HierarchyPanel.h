#pragma once

#include <sonnet/editor/CommandStack.h>
#include <sonnet/editor/Selection.h>
#include <sonnet/editor/TypeFilter.h>

#include <sonnet/assets/AssetDatabase.h>
#include <sonnet/world/Components.h>
#include <sonnet/world/World.h>

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sonnet::editor {

// The drag-and-drop payload of an entity (its 16-byte identity), also accepted by the inspector's
// entity pickers.
constexpr const char *EntityDragPayload = "sonnet_entity";

// The scene tree (docs/editor.md): roots and their children as tree nodes, click to select
// (Ctrl toggles, Shift adds), drag onto another entity or the empty area to reparent, and a
// context menu to focus, expand or collapse a subtree, create primitives, cameras, lights and prefab instances,
// duplicate or delete. Every edit goes through the command stack. A name filter and a multi-select
// kind filter prune the tree to the matches and their ancestors.
class HierarchyPanel {
public:
  // `focus` frames the selection in the viewport: a double-click on a row and the context menu's Focus.
  HierarchyPanel(world::World &world, Selection &selection, CommandStack &commands, std::function<void()> focus);

  // The database that tells a dropped asset's type; without one only prefabs can be dropped.
  void setAssets(const assets::AssetDatabase *assets) noexcept {
    m_assets = assets;
  }

  void draw(bool &open);

  // The context menu's creations, also used by the Edit menu. `parent` nil creates a root.
  void createEntity(std::string_view name, core::Uuid parent, nlohmann::json components = nlohmann::json::object());
  // An entity with a MeshRenderer of the given mesh asset, named after it.
  void createMeshEntity(std::string_view name, core::Uuid mesh, core::Uuid parent);
  void instantiatePrefab(flecs::entity prefab, core::Uuid parent);
  void deleteSelection();
  void duplicateSelection();
  // Opens or closes the entity and everything under it at the next draw; a nil `root` does it for the whole tree.
  void setOpenRecursive(core::Uuid root, bool open);

  // The editor-only filter: a case-insensitive name and the checked entity kinds (none checked means
  // all). An entity matches when its name contains the text and it has a component of a checked kind.
  void setNameFilter(std::string text) {
    m_filter = std::move(text);
  }
  [[nodiscard]] TypeFilter &kindFilter() noexcept {
    return m_kinds;
  }
  [[nodiscard]] bool filtering() const noexcept {
    return !m_filter.empty() || !m_kinds.empty();
  }
  [[nodiscard]] bool matchesFilter(flecs::entity entity) const;
  // The entities the tree lists, in draw order: the matches and their ancestors while filtering, every
  // entity otherwise.
  [[nodiscard]] std::vector<core::Uuid> visibleEntities() const;
  // The kinds the filter offers, in the order of the combo; a TypeFilter over them is indexed by position.
  [[nodiscard]] static std::span<const char *const> kindNames() noexcept;

private:
  [[nodiscard]] bool subtreeMatches(flecs::entity entity) const;
  void collectVisible(flecs::entity entity, std::vector<core::Uuid> &out) const;
  void drawNode(flecs::entity entity);
  void drawBackgroundMenu();
  void drawCreateMenu(core::Uuid parent);
  void drawContextMenu(flecs::entity entity);
  void acceptDrop(core::Uuid newParent);
  void selectClicked(flecs::entity entity);
  void applyOpenToDescendants(flecs::entity entity, bool open);

  world::World &m_world;
  const assets::AssetDatabase *m_assets{nullptr};
  Selection &m_selection;
  CommandStack &m_commands;
  std::function<void()> m_focus;
  struct PendingOpen {
    core::Uuid root;
    bool open;
  };
  std::optional<PendingOpen> m_pendingOpen;
  std::optional<PendingOpen> m_activeOpen;
  std::string m_filter;
  TypeFilter m_kinds;
};

} // namespace sonnet::editor
