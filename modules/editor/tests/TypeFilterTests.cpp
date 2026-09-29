#include <sonnet/editor/TypeFilter.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace sonnet;
using namespace sonnet::editor;

namespace {

assets::AssetInfo asset(std::string name, assets::AssetType type) {
  assets::AssetInfo info;
  info.name = std::move(name);
  info.type = type;
  return info;
}

std::size_t indexOf(assets::AssetType type) {
  const auto types = filterableAssetTypes();
  return static_cast<std::size_t>(std::ranges::find(types, type) - types.begin());
}

} // namespace

TEST_CASE("an empty type filter matches everything and checking narrows to the union", "[editor][filter]") {
  TypeFilter filter;
  REQUIRE(filter.empty());
  REQUIRE(filter.matches(0));
  REQUIRE(filter.matches(5));

  filter.set(1, true);
  filter.set(3, true);
  REQUIRE_FALSE(filter.empty());
  REQUIRE(filter.matches(1));
  REQUIRE(filter.matches(3));
  REQUIRE_FALSE(filter.matches(0));
  REQUIRE_FALSE(filter.matches(2));

  filter.set(1, false);
  filter.set(3, false);
  REQUIRE(filter.empty());
  REQUIRE(filter.matches(2));

  filter.set(2, true);
  filter.clear();
  REQUIRE(filter.empty());
}

TEST_CASE("the type filter summarises what is checked", "[editor][filter]") {
  const auto names = filterableAssetTypeNames();
  TypeFilter filter;
  REQUIRE(filter.summary(names) == "All");
  filter.set(0, true);
  REQUIRE(filter.summary(names) == "Textures");
  filter.set(2, true);
  REQUIRE(filter.summary(names) == "2 types");
  filter.set(4, true);
  REQUIRE(filter.summary(names) == "3 types");
}

TEST_CASE("the asset browser combines the checked types with the name filter", "[editor][filter]") {
  const assets::AssetInfo texture = asset("Brick", assets::AssetType::Texture);
  const assets::AssetInfo material = asset("brick wall", assets::AssetType::Material);
  const assets::AssetInfo mesh = asset("Crate", assets::AssetType::Mesh);
  const assets::AssetInfo model = asset("Helmet", assets::AssetType::Model);

  TypeFilter types;
  for (const auto *info : {&texture, &material, &mesh, &model}) {
    REQUIRE(assetPassesFilter(*info, "", types));
  }

  types.set(indexOf(assets::AssetType::Texture), true);
  types.set(indexOf(assets::AssetType::Material), true);
  REQUIRE(assetPassesFilter(texture, "", types));
  REQUIRE(assetPassesFilter(material, "", types));
  REQUIRE_FALSE(assetPassesFilter(mesh, "", types));
  REQUIRE_FALSE(assetPassesFilter(model, "", types));

  // The name filter is an AND, ignoring case.
  REQUIRE(assetPassesFilter(texture, "BRICK", types));
  REQUIRE(assetPassesFilter(material, "brick", types));
  REQUIRE_FALSE(assetPassesFilter(mesh, "brick", types));
  REQUIRE_FALSE(assetPassesFilter(texture, "crate", types));

  types.clear();
  REQUIRE(assetPassesFilter(model, "hel", types));
  REQUIRE_FALSE(assetPassesFilter(model, "brick", types));
}
