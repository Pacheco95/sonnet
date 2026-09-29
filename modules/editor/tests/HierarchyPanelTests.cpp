#include <sonnet/editor/CommandStack.h>
#include <sonnet/editor/HierarchyPanel.h>
#include <sonnet/editor/Selection.h>

#include <sonnet/world/World.h>

#include <catch2/catch_test_macros.hpp>

#include <imgui.h>
#include <imgui_internal.h>

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
