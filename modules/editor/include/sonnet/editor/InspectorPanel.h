#pragma once

#include <sonnet/editor/CommandStack.h>
#include <sonnet/editor/Selection.h>

#include <sonnet/world/World.h>

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace sonnet::editor {

// The primary selection's name and components, with widgets generated from flecs reflection
// (docs/editor.md, "Inspector"). Values change live while a widget is used; when the widget is
// released one command holding the values before and after goes on the stack.
class InspectorPanel {
public:
  InspectorPanel(world::World &world, Selection &selection, CommandStack &commands);

  void draw(bool &open);

private:
  struct Edit {
    core::Uuid entity;
    flecs::entity_t component{0};
    nlohmann::json before;
  };

  void drawEntity(flecs::entity entity);
  void drawName(flecs::entity entity);
  void drawComponent(flecs::entity entity, const world::ComponentInfo &info);
  void drawAddComponent(flecs::entity entity);
  void drawStruct(flecs::entity type, void *data);
  void drawMember(const ecs_member_t &member, void *data);
  bool drawEnum(flecs::entity type, void *data);
  // Records activation and edits of the last widget for the pending command.
  void track();

  world::World &m_world;
  Selection &m_selection;
  CommandStack &m_commands;
  std::optional<Edit> m_edit;
  bool m_activated{false};
  bool m_deactivatedAfterEdit{false};
  bool m_deactivated{false};
  std::string m_nameBuffer;
  core::Uuid m_nameEntity;
};

} // namespace sonnet::editor
