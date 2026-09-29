#pragma once

#include <sonnet/editor/CommandStack.h>
#include <sonnet/editor/Selection.h>

#include <sonnet/world/Components.h>
#include <sonnet/world/World.h>

#include <functional>
#include <optional>
#include <string_view>

namespace sonnet::editor {

// The scene tree (docs/editor.md): roots and their children as tree nodes, click to select
// (Ctrl toggles, Shift adds), drag onto another entity or the empty area to reparent, and a
// context menu to focus, expand or collapse a subtree, create primitives, cameras, lights and prefab instances,
// duplicate or delete. Every edit goes through the command stack.
class HierarchyPanel {
public:
  // `focus` frames the selection in the viewport: a double-click on a row and the context menu's Focus.
  HierarchyPanel(world::World &world, Selection &selection, CommandStack &commands, std::function<void()> focus);

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

private:
  void drawNode(flecs::entity entity);
  void drawCreateMenu(core::Uuid parent);
  void drawContextMenu(flecs::entity entity);
  void acceptDrop(core::Uuid newParent);
  void selectClicked(flecs::entity entity);
  void applyOpenToDescendants(flecs::entity entity, bool open);

  world::World &m_world;
  Selection &m_selection;
  CommandStack &m_commands;
  std::function<void()> m_focus;
  struct PendingOpen {
    core::Uuid root;
    bool open;
  };
  std::optional<PendingOpen> m_pendingOpen;
  std::optional<PendingOpen> m_activeOpen;
};

} // namespace sonnet::editor
