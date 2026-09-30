#include <sonnet/editor/CommandStack.h>
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
