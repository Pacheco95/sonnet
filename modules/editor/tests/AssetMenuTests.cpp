#include <imgui.h>
#include <imgui_internal.h>
#include <sonnet/core/File.h>
#include <sonnet/editor/AssetBrowserPanel.h>
#include <sonnet/editor/AssetCommands.h>
#include <sonnet/editor/EntityCommands.h>
#include <sonnet/editor/FileManager.h>
#include <sonnet/platform/Platform.h>
#include <sonnet/rhi/NullDevice.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
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
  panel.navigate(root / "assets");
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
      [&](std::span<const std::filesystem::path> files) -> core::Result<void> {
        auto command = deleteFilesCommand(database, world, files);
        if (!command) {
          return std::unexpected(command.error());
        }
        commands.record(std::move(*command));
        return {};
      },
      [](std::span<const core::Uuid>) { return AssetReferences{2, 3, 1}; });
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

TEST_CASE("the folder browser lists disk files and navigates with history and breadcrumbs", "[editor][assets][imgui]") {
  using namespace sonnet;
  BrowserContext context;
  platform::Platform platform{{.headless = true}};
  const auto device = rhi::createNullDevice();
  renderer::Renderer renderer{*device, platform.basePath() / "shaders"};
  core::JobSystem jobs{{.workerCount = 0}};
  assets::AssetDatabase database{renderer, jobs};
  world::World world;
  Selection selection;
  const auto root = std::filesystem::temp_directory_path() / "sonnet_browser_navigation";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "assets/models");
  std::filesystem::create_directories(root / "scenes");
  REQUIRE(core::writeFile(root / "README", "project"));
  REQUIRE(core::writeFile(root / "scenes/main.scene.json", "{}"));
  REQUIRE(core::writeFile(root / "assets/models/data.bin", "buffer"));
  database.open(root, std::vector<std::string>{"assets"});
  std::string saved;
  {
    AssetBrowserPanel panel{database, world, selection};
    const auto frame = [&] {
      ImGui::NewFrame();
      ImGui::SetNextWindowPos({0, 0});
      ImGui::SetNextWindowSize({900, 500});
      bool open = true;
      panel.draw(open);
      ImGui::Render();
    };
    auto &io = ImGui::GetIO();
    const auto key = [&](ImGuiKey pressedKey) {
      io.AddKeyEvent(pressedKey, true);
      frame();
      io.AddKeyEvent(pressedKey, false);
      frame();
    };
    const auto click = [&](float x, float y) {
      io.AddMousePosEvent(x, y);
      frame();
      io.AddMouseButtonEvent(0, true);
      frame();
      io.AddMouseButtonEvent(0, false);
      frame();
    };
    frame();
    frame();
    CHECK(panel.currentFolder() == root);
    auto rows = panel.rows();
    REQUIRE(rows.size() >= 3);
    CHECK(rows[0].directory);
    CHECK(rows[0].name == "assets");
    CHECK(rows[1].directory);
    CHECK(std::ranges::any_of(rows, [](const auto &row) { return row.name == "README" && row.type == "File"; }));
    // Keyboard opens the focused folder; Backspace and Alt arrows navigate without touching the entity selection.
    const auto *table = ImGui::TableFindByID(ImGui::FindWindowByName("Assets")->GetID("assets"));
    REQUIRE(table);
    click(table->Columns[0].MinX + 20, table->OuterRect.Min.y + table->InstanceDataFirst.LastTopHeadersRowHeight +
                                           ImGui::GetStyle().CellPadding.y + ImGui::GetTextLineHeight() * 0.5f);
    key(ImGuiKey_Enter);
    CHECK(panel.currentFolder() == root / "assets");
    key(ImGuiKey_Backspace);
    CHECK(panel.currentFolder() == root);
    io.AddKeyEvent(ImGuiMod_Alt, true);
    key(ImGuiKey_LeftArrow);
    io.AddKeyEvent(ImGuiMod_Alt, false);
    frame();
    CHECK(panel.currentFolder() == root / "assets");
    io.AddKeyEvent(ImGuiMod_Alt, true);
    key(ImGuiKey_RightArrow);
    io.AddKeyEvent(ImGuiMod_Alt, false);
    frame();
    CHECK(panel.currentFolder() == root);
    panel.navigate(root / "assets/models");
    frame();
    CHECK(panel.importFolder() == root / "assets/models");
    rows = panel.rows();
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].name == "data.bin");
    // Root breadcrumb follows the three small navigation buttons.
    const float x = ImGui::GetStyle().WindowPadding.x + ImGui::CalcTextSize("Back").x +
                    ImGui::CalcTextSize("Forward").x + ImGui::CalcTextSize("Up").x +
                    6 * ImGui::GetStyle().FramePadding.x + 3 * ImGui::GetStyle().ItemSpacing.x + 10;
    const float y = ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y + ImGui::GetTextLineHeight() * 0.5f;
    click(x, y);
    CHECK(panel.currentFolder() == root);
    CHECK(panel.importFolder() == root / "assets");
    // Search from the project root reaches unimported files in descendants.
    table = ImGui::TableFindByID(ImGui::FindWindowByName("Assets")->GetID("assets"));
    click(30, table->OuterRect.Min.y - ImGui::GetStyle().ItemSpacing.y - ImGui::GetFrameHeight() * 0.5f);
    io.AddInputCharactersUTF8("DATA");
    frame();
    rows = panel.rows();
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].file == root / "assets/models/data.bin");
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    key(ImGuiKey_A);
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    key(ImGuiKey_Backspace);
    key(ImGuiKey_Enter);
    panel.navigate(root / "scenes");
    std::filesystem::path opened;
    panel.setSceneHandler([&](const auto &file) { opened = file; });
    frame();
    frame();
    table = ImGui::TableFindByID(ImGui::FindWindowByName("Assets")->GetID("assets"));
    click(table->Columns[0].MinX + 20, table->OuterRect.Min.y + table->InstanceDataFirst.LastTopHeadersRowHeight +
                                           ImGui::GetStyle().CellPadding.y + ImGui::GetTextLineHeight() * 0.5f);
    key(ImGuiKey_Enter);
    CHECK(opened == root / "scenes/main.scene.json");
    opened.clear();
    io.DeltaTime = 0.4f;
    frame();
    io.DeltaTime = 1.0f / 60.0f;
    const float sceneX = table->Columns[0].MinX + 20;
    const float sceneY = table->OuterRect.Min.y + table->InstanceDataFirst.LastTopHeadersRowHeight +
                         ImGui::GetStyle().CellPadding.y + ImGui::GetTextLineHeight() * 0.5f;
    click(sceneX, sceneY);
    click(sceneX, sceneY);
    CHECK(opened == root / "scenes/main.scene.json");
    panel.navigate(root / "assets/models");
    std::size_t size = 0;
    const auto *ini = ImGui::SaveIniSettingsToMemory(&size);
    saved.assign(ini, size);
  }
  {
    AssetBrowserPanel restored{database, world, selection};
    ImGui::LoadIniSettingsFromMemory(saved.data(), saved.size());
    CHECK(restored.currentFolder() == root / "assets/models");
    database.close();
  }
  std::filesystem::remove_all(root);
}

TEST_CASE("folder creation rename deletion and import keep one undo step", "[editor][assets][imgui]") {
  using namespace sonnet;
  BrowserContext context;
  platform::Platform platform{{.headless = true}};
  const auto device = rhi::createNullDevice();
  renderer::Renderer renderer{*device, platform.basePath() / "shaders"};
  core::JobSystem jobs{{.workerCount = 0}};
  assets::AssetDatabase database{renderer, jobs};
  world::World world;
  Selection selection;
  const auto root = std::filesystem::temp_directory_path() / "sonnet_browser_folders";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "assets");
  database.open(root, std::vector<std::string>{"assets"});
  CommandStack commands;
  auto created = createFolderCommand(root, root / "assets/Models");
  REQUIRE(created);
  commands.record(std::move(*created));
  REQUIRE(commands.undo(world));
  CHECK_FALSE(std::filesystem::exists(root / "assets/Models"));
  REQUIRE(commands.redo(world));
  CHECK(std::filesystem::is_directory(root / "assets/Models"));
  CHECK_FALSE(createFolderCommand(root, root / "../outside"));
  AssetBrowserPanel panel{database, world, selection};
  panel.navigate(root / "assets");
  panel.setRenameHandler(
      [&](std::span<const std::pair<std::filesystem::path, std::filesystem::path>> moves) -> core::Result<void> {
        std::vector<std::unique_ptr<ICommand>> parts;
        for (const auto &[from, to] : moves) {
          auto command = moveFilesCommand(database, world, from, to);
          if (!command) {
            return std::unexpected(command.error());
          }
          parts.push_back(std::move(*command));
        }
        commands.record(compositeCommand("rename selection", std::move(parts)));
        return {};
      });
  panel.setFileHandlers(
      [&](const auto &from, const auto &to) -> core::Result<void> {
        auto command = moveFilesCommand(database, world, from, to);
        if (!command) {
          return std::unexpected(command.error());
        }
        commands.record(std::move(*command));
        return {};
      },
      [&](std::span<const std::filesystem::path> files) -> core::Result<void> {
        auto command = deleteFilesCommand(database, world, files);
        if (!command) {
          return std::unexpected(command.error());
        }
        commands.record(std::move(*command));
        return {};
      },
      [](std::span<const core::Uuid>) { return AssetReferences{}; });
  const auto frame = [&] {
    ImGui::NewFrame();
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({900, 500});
    bool open = true;
    panel.draw(open);
    ImGui::Render();
  };
  auto &io = ImGui::GetIO();
  const auto key = [&](ImGuiKey pressedKey) {
    io.AddKeyEvent(pressedKey, true);
    frame();
    io.AddKeyEvent(pressedKey, false);
    frame();
  };
  const auto click = [&](float x, float y) {
    io.AddMousePosEvent(x, y);
    frame();
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMouseButtonEvent(0, false);
    frame();
    io.DeltaTime = 0.4f;
    frame();
    io.DeltaTime = 1.0f / 60.0f;
  };
  frame();
  frame();
  const auto *table = ImGui::TableFindByID(ImGui::FindWindowByName("Assets")->GetID("assets"));
  REQUIRE(table);
  click(table->Columns[0].MinX + 20, table->OuterRect.Min.y + table->InstanceDataFirst.LastTopHeadersRowHeight +
                                         ImGui::GetStyle().CellPadding.y + ImGui::GetTextLineHeight() * 0.5f);
  key(ImGuiKey_F2);
  frame();
  io.AddKeyEvent(ImGuiMod_Ctrl, true);
  key(ImGuiKey_A);
  io.AddKeyEvent(ImGuiMod_Ctrl, false);
  io.AddInputCharactersUTF8("Renamed");
  frame();
  key(ImGuiKey_Enter);
  frame();
  REQUIRE(std::filesystem::is_directory(root / "assets/Renamed"));
  REQUIRE(commands.undo(world));
  CHECK(std::filesystem::is_directory(root / "assets/Models"));
  REQUIRE(commands.redo(world));
  panel.navigate(root / "assets/Renamed");
  REQUIRE(core::writeFile(root / "source.lua", "return {}"));
  const std::array sources{root / "source.lua"};
  panel.setImportHandler([&] {
    auto command = importAssets(database, world, sources, panel.importFolder());
    REQUIRE(command);
    commands.record(std::move(*command));
  });
  frame();
  frame();
  // Import is the fourth control on the filter row.
  const auto *window = ImGui::FindWindowByName("Assets");
  const float importX = window->Pos.x + ImGui::GetStyle().WindowPadding.x + 160 + 130 +
                        ImGui::CalcTextSize("New asset").x + 2 * ImGui::GetStyle().FramePadding.x +
                        3 * ImGui::GetStyle().ItemSpacing.x + 15;
  const float importY = table->OuterRect.Min.y - ImGui::GetStyle().ItemSpacing.y - ImGui::GetFrameHeight() * 0.5f;
  click(importX, importY);
  REQUIRE(std::filesystem::exists(root / "assets/Renamed/source.lua"));
  REQUIRE(commands.undo(world));
  CHECK_FALSE(std::filesystem::exists(root / "assets/Renamed/source.lua"));
  REQUIRE(commands.redo(world));
  auto second = createFolderCommand(root, root / "assets/Second");
  REQUIRE(second);
  commands.record(std::move(*second));
  panel.up();
  frame();
  frame();
  table = ImGui::TableFindByID(ImGui::FindWindowByName("Assets")->GetID("assets"));
  click(table->Columns[0].MinX + 20, table->OuterRect.Min.y + table->InstanceDataFirst.LastTopHeadersRowHeight +
                                         ImGui::GetStyle().CellPadding.y + ImGui::GetTextLineHeight() * 0.5f);
  const float rowHeight = ImGui::GetTextLineHeight() + 2 * ImGui::GetStyle().CellPadding.y;
  const float secondY = table->OuterRect.Min.y + table->InstanceDataFirst.LastTopHeadersRowHeight +
                        ImGui::GetStyle().CellPadding.y + ImGui::GetTextLineHeight() * 0.5f + rowHeight;
  io.AddKeyEvent(ImGuiMod_Shift, true);
  click(table->Columns[0].MinX + 20, secondY);
  io.AddKeyEvent(ImGuiMod_Shift, false);
  frame();
  // Ctrl toggles one row without clearing the range, then restores it.
  io.AddKeyEvent(ImGuiMod_Ctrl, true);
  click(table->Columns[0].MinX + 20, secondY);
  click(table->Columns[0].MinX + 20, secondY);
  io.AddKeyEvent(ImGuiMod_Ctrl, false);
  frame();
  const auto beforeRename = commands.size();
  key(ImGuiKey_F2);
  frame();
  auto *renameModal = ImGui::FindWindowByName("Rename selection");
  REQUIRE(renameModal);
  const float inputX = renameModal->Pos.x + ImGui::GetStyle().WindowPadding.x + 30;
  const float inputY = renameModal->Pos.y + ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y +
                       ImGui::GetTextLineHeight() + ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeight() * 0.5f;
  const auto renameInput = [&](float y, const char *text) {
    click(inputX, y);
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    key(ImGuiKey_A);
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    io.AddInputCharactersUTF8(text);
    frame();
  };
  renameInput(inputY, "First renamed");
  renameInput(inputY + ImGui::GetTextLineHeight() + ImGui::GetFrameHeight() + 2 * ImGui::GetStyle().ItemSpacing.y,
              "Second renamed");
  click(renameModal->Pos.x + ImGui::GetStyle().WindowPadding.x + 20,
        renameModal->Pos.y + renameModal->Size.y - ImGui::GetStyle().WindowPadding.y - ImGui::GetFrameHeight() * 0.5f);
  REQUIRE(std::filesystem::exists(root / "assets/First renamed/source.lua"));
  REQUIRE(std::filesystem::is_directory(root / "assets/Second renamed"));
  CHECK(commands.size() == beforeRename + 1);
  REQUIRE(commands.undo(world));
  REQUIRE(std::filesystem::is_directory(root / "assets/Renamed"));
  REQUIRE(std::filesystem::is_directory(root / "assets/Second"));
  REQUIRE(commands.redo(world));
  CHECK(std::filesystem::exists(root / "assets/First renamed/source.lua"));
  REQUIRE(commands.undo(world));
  frame();
  frame();
  table = ImGui::TableFindByID(ImGui::FindWindowByName("Assets")->GetID("assets"));
  click(table->Columns[0].MinX + 20, secondY - rowHeight);
  io.AddKeyEvent(ImGuiMod_Shift, true);
  click(table->Columns[0].MinX + 20, secondY);
  io.AddKeyEvent(ImGuiMod_Shift, false);
  frame();
  const auto beforeDelete = commands.size();
  key(ImGuiKey_Delete);
  frame();
  auto *modal = ImGui::FindWindowByName("Delete asset?");
  REQUIRE(modal);
  click(modal->Pos.x + ImGui::GetStyle().WindowPadding.x + 20,
        modal->Pos.y + modal->Size.y - ImGui::GetStyle().WindowPadding.y - ImGui::GetFrameHeight() * 0.5f);
  CHECK_FALSE(std::filesystem::exists(root / "assets/Renamed"));
  CHECK_FALSE(std::filesystem::exists(root / "assets/Second"));
  CHECK(commands.size() == beforeDelete + 1);
  REQUIRE(commands.undo(world));
  CHECK(std::filesystem::is_directory(root / "assets/Second"));
  CHECK(std::filesystem::exists(root / "assets/Renamed/source.lua"));
  CHECK(std::ranges::any_of(database.assets(),
                            [&](const auto *info) { return info->source == root / "assets/Renamed/source.lua"; }));
  commands.clear();
  database.close();
  std::filesystem::remove_all(root);
}
