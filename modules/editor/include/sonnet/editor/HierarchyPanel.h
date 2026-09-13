#pragma once

#include <sonnet/editor/CommandStack.h>
#include <sonnet/editor/Selection.h>

#include <sonnet/world/Components.h>
#include <sonnet/world/World.h>

#include <string_view>

namespace sonnet::editor {

// The scene tree (docs/editor.md): roots and their children as tree nodes, click to select
// (Ctrl toggles, Shift adds), drag onto another entity or the empty area to reparent, and a
// context menu to create primitives, cameras, lights and prefab instances, duplicate or delete.
// Every edit goes through the command stack.
class HierarchyPanel {
public:
  HierarchyPanel(world::World &world, Selection &selection, CommandStack &commands);

  void draw(bool &open);

  // The context menu's creations, also used by the Edit menu. `parent` nil creates a root.
  void createEntity(std::string_view name, core::Uuid parent, nlohmann::json components = nlohmann::json::object());
  void createPrimitive(world::Primitive primitive, core::Uuid parent);
  void instantiatePrefab(flecs::entity prefab, core::Uuid parent);
  void deleteSelection();
  void duplicateSelection();

private:
  void drawNode(flecs::entity entity);
  void drawCreateMenu(core::Uuid parent);
  void drawContextMenu(flecs::entity entity);
  void acceptDrop(core::Uuid newParent);
  void selectClicked(flecs::entity entity);

  world::World &m_world;
  Selection &m_selection;
  CommandStack &m_commands;
};

} // namespace sonnet::editor
