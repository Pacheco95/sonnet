#include <sonnet/editor/AssetBrowserPanel.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

using namespace sonnet::editor;

TEST_CASE("directoryUrl makes a file URL the platform can open", "[editor][assets]") {
  CHECK(directoryUrl("/home/me/game/scripts") == "file:///home/me/game/scripts");
  CHECK(directoryUrl("/home/me/my game/s\u00e9") == "file:///home/me/my%20game/s%C3%A9");
  CHECK(directoryUrl("C:/Users/me/game") == "file:///C:/Users/me/game");
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
