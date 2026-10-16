#include <imgui.h>
#include <imgui_internal.h>
#include <sonnet/editor/AssetBrowserPanel.h>
#include <sonnet/editor/AssetCommands.h>
#include <sonnet/editor/FileManager.h>
#include <sonnet/platform/Platform.h>
#include <sonnet/rhi/NullDevice.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

using namespace sonnet::editor;

TEST_CASE("fileUrl makes a file URL the platform can open", "[editor][assets]") {
  CHECK(fileUrl("/home/me/game/scripts") == "file:///home/me/game/scripts");
  CHECK(fileUrl("/home/me/my game/s\u00e9") == "file:///home/me/my%20game/s%C3%A9");
  CHECK(fileUrl("C:/Users/me/game") == "file:///C:/Users/me/game");
  // dbus-send splits an array argument at commas, so they must be encoded.
  CHECK(fileUrl("/home/me/a,b.lua") == "file:///home/me/a%2Cb.lua");
}

namespace {

// Clang warns about designated initialisers that skip a field, so the tests' assets are built by assignment.
sonnet::assets::AssetInfo makeAsset(sonnet::assets::AssetType type, std::filesystem::path source, std::string name,
                                    sonnet::core::Uuid parent = {}) {
  sonnet::assets::AssetInfo info;
  info.uuid = sonnet::core::Uuid::generate();
  info.type = type;
  info.source = std::move(source);
  info.name = std::move(name);
  info.parent = parent;
  return info;
}

} // namespace

TEST_CASE("sortAssetRows orders the asset browser by a column", "[editor][assets]") {
  using sonnet::assets::AssetInfo;
  using sonnet::assets::AssetType;
  const std::filesystem::path root = "/game";
  const std::vector<AssetInfo> infos{
      makeAsset(AssetType::Texture, "/game/b/Zebra.png", "Zebra"),
      makeAsset(AssetType::Material, "/game/A/apple.material.json", "apple"),
      makeAsset(AssetType::Texture, "/game/c/Mango.png", "Mango"),
      makeAsset(AssetType::Material, "builtin", "Default"),
  };
  std::vector<AssetRow> rows;
  rows.reserve(infos.size());
  for (const AssetInfo &info : infos) {
    rows.push_back(assetRow(info, root));
  }
  const auto names = [&] {
    std::vector<std::string> result;
    result.reserve(rows.size());
    for (const AssetRow &row : rows) {
      result.push_back(row.name);
    }
    return result;
  };

  sortAssetRows(rows, AssetSortColumn::Name, false);
  CHECK(names() == std::vector<std::string>{"apple", "Default", "Mango", "Zebra"});
  sortAssetRows(rows, AssetSortColumn::Name, true);
  CHECK(names() == std::vector<std::string>{"Zebra", "Mango", "Default", "apple"});
  // Ties within a type go by name ascending, whichever way the type runs.
  sortAssetRows(rows, AssetSortColumn::Type, false);
  CHECK(names() == std::vector<std::string>{"apple", "Default", "Mango", "Zebra"});
  sortAssetRows(rows, AssetSortColumn::Type, true);
  CHECK(names() == std::vector<std::string>{"Mango", "Zebra", "apple", "Default"});
  sortAssetRows(rows, AssetSortColumn::Source, false);
  CHECK(names() == std::vector<std::string>{"apple", "Zebra", "Default", "Mango"});
}

TEST_CASE("assetSourceLabel is what the Source column shows", "[editor][assets]") {
  using sonnet::assets::AssetType;
  CHECK(assetSourceLabel(makeAsset(AssetType::Material, "builtin", "Default"), "/game") == "built-in");
  CHECK(assetSourceLabel(makeAsset(AssetType::Model, "/game/models/ship.glb", "ship"), "/game") == "models/ship.glb");
  CHECK(assetSourceLabel(makeAsset(AssetType::Mesh, "/game/models/ship.glb", "Hull", sonnet::core::Uuid::generate()),
                         "/game") == "models/ship.glb / Hull");
}

TEST_CASE("a prefab row lists its file and sorts by its type among the assets", "[editor][assets]") {
  using sonnet::assets::AssetType;
  const std::filesystem::path root = "/game";
  const sonnet::assets::AssetInfo model = makeAsset(AssetType::Model, "/game/models/ship.glb", "ship");
  const sonnet::assets::AssetInfo script = makeAsset(AssetType::Script, "/game/scripts/turret.lua", "turret");
  const AssetRow prefab = prefabRow(sonnet::core::Uuid::generate(), "Crate", "/game/prefabs/Crate.prefab.json", root);
  CHECK(prefab.info == nullptr);
  CHECK(prefab.type == "Prefab");
  CHECK(prefab.source == "prefabs/Crate.prefab.json");
  CHECK(prefab.file == std::filesystem::path{"/game/prefabs/Crate.prefab.json"});

  std::vector<AssetRow> rows{assetRow(script, root), prefab, assetRow(model, root)};
  sortAssetRows(rows, AssetSortColumn::Type, false);
  CHECK(rows[0].name == "ship");
  CHECK(rows[1].name == "Crate");
  CHECK(rows[2].name == "turret");
}

namespace {
struct BrowserContext {
  BrowserContext() {
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.DisplaySize = {900.0f, 600.0f};
    io.IniFilename = nullptr;
    unsigned char *pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
  }
  ~BrowserContext() {
    ImGui::DestroyContext();
  }
};
} // namespace

TEST_CASE("the asset browser renames with F2 and confirms Delete", "[editor][assets][imgui]") {
  using namespace sonnet;
  BrowserContext context;
  platform::Platform platform{{.headless = true}};
  const auto device = rhi::createNullDevice();
  renderer::Renderer renderer{*device, platform.basePath() / "shaders"};
  core::JobSystem jobs{{.workerCount = 0}};
  assets::AssetDatabase database{renderer, jobs};
  world::World world;
  Selection selection;
  const auto root = std::filesystem::temp_directory_path() / "sonnet_browser_file_actions";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "assets");
  const std::vector<std::string> roots{"assets"};
  database.open(root, roots);
  const auto id = database.createScript(root / "assets/aaa.lua", "return {}");
  REQUIRE(id);
  AssetBrowserPanel panel{database, world, selection};
  CommandStack commands;
  panel.setFileHandlers(
      [&](const auto &from, const auto &to) -> core::Result<void> {
        auto command = moveFilesCommand(database, world, from, to);
        if (!command) {
          return std::unexpected(command.error());
        }
        commands.record(std::move(*command));
        return {};
      },
      [&](const auto &file) -> core::Result<void> {
        const std::vector<std::filesystem::path> files{file};
        auto command = deleteFilesCommand(database, world, files);
        if (!command) {
          return std::unexpected(command.error());
        }
        commands.record(std::move(*command));
        return {};
      },
      [](core::Uuid) { return AssetReferences{2, 3, 1}; });
  const auto frame = [&] {
    ImGui::NewFrame();
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({900, 500});
    bool open = true;
    panel.draw(open);
    ImGui::Render();
  };
  auto &io = ImGui::GetIO();
  frame();
  frame();
  ImGui::FocusWindow(ImGui::FindWindowByName("Assets"));
  const auto *table = ImGui::TableFindByID(ImGui::FindWindowByName("Assets")->GetID("assets"));
  REQUIRE(table != nullptr);
  io.AddMousePosEvent(table->Columns[0].MinX + 20.0f,
                      table->OuterRect.Min.y + table->InstanceDataFirst.LastTopHeadersRowHeight +
                          ImGui::GetStyle().CellPadding.y + ImGui::GetTextLineHeight() * 0.5f);
  frame();
  io.AddMouseButtonEvent(0, true);
  frame();
  io.AddMouseButtonEvent(0, false);
  frame();
  REQUIRE(panel.inspected() == *id);
  io.AddKeyEvent(ImGuiKey_F2, true);
  frame();
  io.AddKeyEvent(ImGuiKey_F2, false);
  frame();
  frame();
  io.AddKeyEvent(ImGuiMod_Ctrl, true);
  io.AddKeyEvent(ImGuiKey_A, true);
  frame();
  io.AddKeyEvent(ImGuiKey_A, false);
  io.AddKeyEvent(ImGuiMod_Ctrl, false);
  io.AddInputCharactersUTF8("renamed");
  frame();
  io.AddKeyEvent(ImGuiKey_Enter, true);
  frame();
  io.AddKeyEvent(ImGuiKey_Enter, false);
  frame();
  frame();
  REQUIRE(database.find(*id)->source == root / "assets/renamed.lua");
  io.AddKeyEvent(ImGuiKey_Delete, true);
  frame();
  io.AddKeyEvent(ImGuiKey_Delete, false);
  frame();
  frame();
  REQUIRE(ImGui::IsPopupOpen("Delete asset?", ImGuiPopupFlags_AnyPopupId));
  REQUIRE(database.find(*id) != nullptr);
  auto *modal = ImGui::FindWindowByName("Delete asset?");
  REQUIRE(modal != nullptr);
  const ImVec2 button{modal->Pos.x + ImGui::GetStyle().WindowPadding.x + 20.0f, modal->Pos.y + modal->Size.y -
                                                                                    ImGui::GetStyle().WindowPadding.y -
                                                                                    ImGui::GetFrameHeight() * 0.5f};
  io.AddMousePosEvent(button.x, button.y);
  frame();
  io.AddMouseButtonEvent(0, true);
  frame();
  io.AddMouseButtonEvent(0, false);
  frame();
  CHECK(database.find(*id) == nullptr);
  CHECK(commands.size() == 2);
  REQUIRE(commands.undo(world));
  CHECK(database.find(*id) != nullptr);
  commands.clear();
  std::filesystem::remove_all(root);
}
