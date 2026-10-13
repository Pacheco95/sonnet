#include <sonnet/editor/AssetBrowserPanel.h>

#include <catch2/catch_test_macros.hpp>

using namespace sonnet::editor;

TEST_CASE("directoryUrl makes a file URL the platform can open", "[editor][assets]") {
  CHECK(directoryUrl("/home/me/game/scripts") == "file:///home/me/game/scripts");
  CHECK(directoryUrl("/home/me/my game/s\u00e9") == "file:///home/me/my%20game/s%C3%A9");
  CHECK(directoryUrl("C:/Users/me/game") == "file:///C:/Users/me/game");
}
