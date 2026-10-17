#include <sonnet/editor/CommandStack.h>
#include <sonnet/editor/EntityCommands.h>
#include <sonnet/editor/HierarchyPanel.h>
#include <sonnet/editor/Selection.h>

#include <sonnet/assets/Asset.h>
#include <sonnet/world/Components.h>
#include <sonnet/world/Scene.h>
#include <sonnet/world/World.h>

#include <catch2/catch_test_macros.hpp>

#include <imgui.h>
#include <imgui_internal.h>

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

using namespace sonnet;

namespace {

// A Dear ImGui context without a renderer: enough to lay out the panel's tree nodes.
struct ImGuiContextGuard {
  ImGuiContextGuard() {
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2{800.0f, 600.0f};
    io.IniFilename = nullptr;
    unsigned char *pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
  }
  ~ImGuiContextGuard() {
    ImGui::DestroyContext();
  }
  ImGuiContextGuard(const ImGuiContextGuard &) = delete;
  ImGuiContextGuard &operator=(const ImGuiContextGuard &) = delete;
};

void frame(editor::HierarchyPanel &panel) {
  ImGui::NewFrame();
  bool open = true;
  panel.draw(open);
  ImGui::Render();
}

// Whether the row of `entity` (children of `root` ... `entity`, by pick id) is open in the panel's storage.
bool isOpen(flecs::entity path[], int count) {
  ImGuiWindow *window = ImGui::FindWindowByName("Hierarchy");
  REQUIRE(window != nullptr);
  ImGuiID id = window->ID;
  for (int i = 0; i < count; ++i) {
    const int pick = static_cast<int>(world::World::pickId(path[i]));
    id = ImHashStr("node", 0, ImHashData(&pick, sizeof(pick), id));
  }
  return window->StateStorage.GetInt(id, 1) != 0;
}

} // namespace

TEST_CASE("expand all and collapse all open and close a whole subtree", "[editor][hierarchy]") {
  ImGuiContextGuard imgui;
  world::World world;
  editor::Selection selection;
  editor::CommandStack commands;
  editor::HierarchyPanel panel(world, selection, commands, [] {});
  const flecs::entity root = world.createEntity("Root");
  const flecs::entity child = world.createEntity("Child", root);
  const flecs::entity grandchild = world.createEntity("Grandchild", child);
  world.createEntity("Leaf", grandchild);
  flecs::entity path[] = {root, child, grandchild};

  frame(panel);
  REQUIRE(isOpen(path, 3));

  panel.setOpenRecursive(world.uuidOf(root), false);
  frame(panel);
  CHECK_FALSE(isOpen(path, 1));
  CHECK_FALSE(isOpen(path, 2));
  CHECK_FALSE(isOpen(path, 3));

  // The request applies once: a later frame keeps whatever the user did meanwhile.
  frame(panel);
  CHECK_FALSE(isOpen(path, 1));

  panel.setOpenRecursive({}, true);
  frame(panel);
  CHECK(isOpen(path, 1));
  CHECK(isOpen(path, 2));
  CHECK(isOpen(path, 3));
}

TEST_CASE("the hierarchy filter keeps the matches and their ancestors", "[editor][hierarchy]") {
  ImGuiContextGuard imgui;
  world::World world;
  editor::Selection selection;
  editor::CommandStack commands;
  editor::HierarchyPanel panel(world, selection, commands, [] {});
  const flecs::entity rig = world.createEntity("Rig");
  const flecs::entity lamp = world.createEntity("Lamp", rig);
  lamp.add<world::PointLight>();
  const flecs::entity sun = world.createEntity("Sun");
  sun.add<world::DirectionalLight>();
  const flecs::entity eye = world.createEntity("Eye", rig);
  eye.add<world::Camera>();
  const flecs::entity crate = world.createEntity("Crate");
  crate.add<world::MeshRenderer>();
  const auto uuids = [&](std::initializer_list<flecs::entity> entities) {
    std::vector<core::Uuid> out;
    for (const flecs::entity e : entities) {
      out.push_back(world.uuidOf(e));
    }
    return out;
  };

  // No filter: everything, in tree order.
  REQUIRE_FALSE(panel.filtering());
  REQUIRE(panel.visibleEntities() == uuids({rig, lamp, eye, sun, crate}));

  // One kind: its entities and their ancestors.
  const std::size_t lights = 3;
  REQUIRE(std::string{editor::HierarchyPanel::kindNames()[lights]} == "Lights");
  panel.kindFilter().set(lights, true);
  REQUIRE(panel.filtering());
  REQUIRE(panel.visibleEntities() == uuids({rig, lamp, sun}));
  CHECK(panel.matchesFilter(lamp));
  CHECK_FALSE(panel.matchesFilter(rig));

  // Several kinds are a union.
  panel.kindFilter().set(4, true);
  REQUIRE(panel.visibleEntities() == uuids({rig, lamp, eye, sun}));

  // The name narrows further, ignoring case.
  panel.setNameFilter("LAM");
  REQUIRE(panel.visibleEntities() == uuids({rig, lamp}));
  panel.setNameFilter("crate");
  REQUIRE(panel.visibleEntities().empty());

  // The name alone matches any kind; clearing both restores the tree.
  panel.kindFilter().clear();
  REQUIRE(panel.visibleEntities() == uuids({crate}));
  panel.setNameFilter("");
  REQUIRE_FALSE(panel.filtering());
  REQUIRE(panel.visibleEntities() == uuids({rig, lamp, eye, sun, crate}));

  // Drawing a filtered tree opens the ancestors of the matches even when the user closed them.
  panel.setOpenRecursive({}, false);
  frame(panel);
  panel.kindFilter().set(lights, true);
  frame(panel);
  flecs::entity path[] = {rig};
  CHECK(isOpen(path, 1));
}

TEST_CASE("every built-in shape can be created, saved and loaded again", "[editor][hierarchy]") {
  const std::filesystem::path path = std::filesystem::temp_directory_path() / "sonnet_primitives.scene.json";
  {
    world::World world;
    editor::Selection selection;
    editor::CommandStack commands;
    editor::HierarchyPanel panel(world, selection, commands, [] {});
    for (const assets::builtin::Entry &entry : assets::builtin::all()) {
      panel.createMeshEntity(entry.name, entry.uuid(), {});
    }
    REQUIRE(world::saveSceneFile(world, path).has_value());
  }
  world::World reopened;
  REQUIRE(world::loadSceneFile(reopened, path).has_value());
  // Roots come back in creation order, one per shape.
  const std::vector<flecs::entity> roots = reopened.roots();
  REQUIRE(roots.size() == assets::builtin::all().size());
  for (std::size_t i = 0; i < roots.size(); ++i) {
    REQUIRE(roots[i].get<world::MeshRenderer>().mesh == assets::builtin::all()[i].uuid());
    REQUIRE(roots[i].get<world::Name>().value == assets::builtin::all()[i].name);
  }
  std::filesystem::remove(path);
}

// Regression: "Save as prefab" from the row's context menu destroyed the entity and its children
// while the tree was still drawing them, and the next read of a child asserted in flecs.
TEST_CASE("the context menu's save as prefab runs after the tree is drawn", "[editor][hierarchy][prefab]") {
  ImGuiContextGuard imgui;
  world::World world;
  editor::Selection selection;
  editor::CommandStack commands;
  editor::HierarchyPanel panel(world, selection, commands, [] {});
  panel.setSaveAsPrefab([&](core::Uuid entity) {
    const auto prefab = world::loadPrefab(world, editor::prefabFromSubtree(world, entity));
    REQUIRE(prefab.has_value());
    commands.push(editor::replaceWithInstanceCommand(world, entity, world.uuidOf(*prefab)), world);
  });
  const flecs::entity colonnade = world.createEntity("Colonnade");
  world.createEntity("Column", colonnade);
  world.createEntity("Column", colonnade);
  const core::Uuid uuid = world.uuidOf(colonnade);
  frame(panel);

  ImGuiIO &io = ImGui::GetIO();
  // Moves the mouse down `window` until the item `id` is hovered.
  const auto hover = [&](const ImGuiWindow *window, ImGuiID id) {
    for (float y = window->Pos.y; y < window->Pos.y + window->Size.y; y += 2.0f) {
      io.AddMousePosEvent(window->Pos.x + 40.0f, y);
      frame(panel);
      if (ImGui::GetCurrentContext()->HoveredIdPreviousFrame == id) {
        return true;
      }
    }
    return false;
  };
  const auto click = [&](ImGuiMouseButton button) {
    io.AddMouseButtonEvent(button, true);
    frame(panel);
    io.AddMouseButtonEvent(button, false);
    frame(panel);
  };
  ImGuiWindow *hierarchy = ImGui::FindWindowByName("Hierarchy");
  REQUIRE(hierarchy != nullptr);
  const int pick = static_cast<int>(world::World::pickId(colonnade));
  REQUIRE(hover(hierarchy, ImHashStr("node", 0, ImHashData(&pick, sizeof(pick), hierarchy->ID))));
  click(ImGuiMouseButton_Right);
  REQUIRE(!ImGui::GetCurrentContext()->OpenPopupStack.empty());
  ImGuiWindow *menu = ImGui::GetCurrentContext()->OpenPopupStack.back().Window;
  REQUIRE(menu != nullptr);
  REQUIRE(hover(menu, ImHashStr("Save as prefab", 0, menu->ID)));
  click(ImGuiMouseButton_Left);

  REQUIRE(world.isInstance(world.find(uuid)));
  REQUIRE(world.children(world.find(uuid)).size() == 2);
  frame(panel);
}

TEST_CASE("a row moves among its siblings, an instance's own children included", "[editor][hierarchy]") {
  world::World world;
  editor::Selection selection;
  editor::CommandStack commands;
  editor::HierarchyPanel panel(world, selection, commands, [] {});
  const flecs::entity prefab = world.createEntity("Crate");
  world.createEntity("Lid", prefab);
  world.makePrefab(prefab);
  const flecs::entity a = world.createEntity("A");
  const flecs::entity b = world.createEntity("B");
  const flecs::entity instance = world.instantiate(prefab, "Crate 1");
  const flecs::entity part = world.children(instance).front();
  const flecs::entity x = world.createEntity("X", instance);
  const flecs::entity y = world.createEntity("Y", instance);
  const auto uuid = [&](flecs::entity entity) { return world.uuidOf(entity); };
  const auto names = [&](const std::vector<flecs::entity> &entities) {
    std::vector<std::string> result;
    result.reserve(entities.size());
    for (const flecs::entity entity : entities) {
      result.push_back(entity.get<world::Name>().value);
    }
    return result;
  };
  using Names = std::vector<std::string>;

  // No change, the entity under itself or a descendant, and part of an instance are refused.
  CHECK_FALSE(panel.canMove(uuid(a), {}, 0));
  CHECK_FALSE(panel.canMove(uuid(a), uuid(a), 0));
  CHECK_FALSE(panel.canMove(uuid(instance), uuid(x), 0));
  CHECK_FALSE(panel.canMove(uuid(part), uuid(instance), 0));
  CHECK_FALSE(panel.canMove(uuid(part), {}, 0)); // a copy keeps its parent
  CHECK_FALSE(panel.canMove(uuid(a), core::Uuid::generate(), 0));

  REQUIRE(panel.moveEntity(uuid(b), {}, 0));
  REQUIRE(names(world.roots()) == Names{"B", "A", "Crate 1"});
  REQUIRE(commands.undo(world));
  REQUIRE(names(world.roots()) == Names{"A", "B", "Crate 1"});

  // Added children go before the instance's own, and those move among the others.
  REQUIRE(panel.moveEntity(uuid(y), uuid(instance), 0));
  REQUIRE(names(world.children(instance)) == Names{"Y", "Lid", "X"});
  REQUIRE(panel.moveEntity(uuid(a), uuid(instance), 1));
  REQUIRE(names(world.children(instance)) == Names{"Y", "A", "Lid", "X"});
  REQUIRE(panel.moveEntity(uuid(part), uuid(instance), 3));
  REQUIRE(names(world.children(instance)) == Names{"Y", "A", "X", "Lid"});
  REQUIRE(commands.undo(world));
  REQUIRE(names(world.children(instance)) == Names{"Y", "A", "Lid", "X"});
  REQUIRE(commands.redo(world));

  // The order survives the save and load that play mode and a reopen do.
  const nlohmann::json scene = world::saveScene(world);
  world.clearScene();
  REQUIRE(world::loadScene(world, scene));
  REQUIRE(names(world.roots()) == Names{"B", "Crate 1"});
  REQUIRE(names(world.children(world.roots().back())) == Names{"Y", "A", "X", "Lid"});
}

TEST_CASE("dragging a row onto the upper edge of another puts it before that row", "[editor][hierarchy]") {
  ImGuiContextGuard imgui;
  world::World world;
  editor::Selection selection;
  editor::CommandStack commands;
  editor::HierarchyPanel panel(world, selection, commands, [] {});
  const flecs::entity a = world.createEntity("A");
  world.createEntity("B");
  const flecs::entity c = world.createEntity("C");
  frame(panel);

  ImGuiIO &io = ImGui::GetIO();
  ImGuiWindow *hierarchy = ImGui::FindWindowByName("Hierarchy");
  REQUIRE(hierarchy != nullptr);
  // The rows' vertical extents, found by moving the mouse down the window a pixel at a time.
  const auto span = [&](flecs::entity entity) {
    const int pick = static_cast<int>(world::World::pickId(entity));
    const ImGuiID id = ImHashStr("node", 0, ImHashData(&pick, sizeof(pick), hierarchy->ID));
    float top = -1.0f;
    float bottom = -1.0f;
    for (float y = hierarchy->Pos.y; y < hierarchy->Pos.y + hierarchy->Size.y; y += 1.0f) {
      io.AddMousePosEvent(hierarchy->Pos.x + 40.0f, y);
      frame(panel);
      if (ImGui::GetCurrentContext()->HoveredIdPreviousFrame == id) {
        top = top < 0.0f ? y : top;
        bottom = y;
      }
    }
    return std::pair{top, bottom};
  };
  const auto [aTop, aBottom] = span(a);
  const auto [cTop, cBottom] = span(c);
  REQUIRE(aTop >= 0.0f);
  REQUIRE(cTop > aBottom);

  const float x = hierarchy->Pos.x + 40.0f;
  io.AddMousePosEvent(x, (cTop + cBottom) * 0.5f);
  frame(panel);
  io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
  frame(panel);
  for (float y = (cTop + cBottom) * 0.5f; y > aTop; y -= 4.0f) {
    io.AddMousePosEvent(x, y);
    frame(panel);
  }
  io.AddMousePosEvent(x, aTop);
  frame(panel);
  frame(panel);
  io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
  frame(panel);
  frame(panel);

  std::vector<std::string> names;
  for (const flecs::entity root : world.roots()) {
    names.push_back(root.get<world::Name>().value);
  }
  REQUIRE(names == std::vector<std::string>{"C", "A", "B"});
  REQUIRE(commands.undoDescription() == "reorder C");
}

TEST_CASE("an environment dropped on the hierarchy becomes the scene's sky", "[editor][hierarchy]") {
  world::World world;
  editor::Selection selection;
  editor::CommandStack commands;
  editor::HierarchyPanel panel(world, selection, commands, [] {});
  const core::Uuid sky = core::Uuid::generate();
  const core::Uuid dusk = core::Uuid::generate();

  // No Environment yet: a new root, named after the asset and selected.
  panel.useEnvironment(sky, "sky");
  REQUIRE(world.roots().size() == 1);
  const flecs::entity entity = world.roots().front();
  REQUIRE(entity.get<world::Name>().value == "sky");
  REQUIRE(entity.get<world::Environment>().map == sky);
  REQUIRE(selection.contains(world.uuidOf(entity)));

  // One already: its map changes and its other settings stay, as one undoable command.
  entity.set<world::Environment>({.map = sky, .intensity = 0.25f});
  panel.useEnvironment(dusk, "dusk");
  REQUIRE(world.roots().size() == 1);
  CHECK(entity.get<world::Environment>().map == dusk);
  CHECK(entity.get<world::Environment>().intensity == 0.25f);
  REQUIRE(commands.undo(world));
  CHECK(world.find(world.uuidOf(entity)).get<world::Environment>().map == sky);
}
