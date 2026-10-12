#include <sonnet/runtime/GameModule.h>

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace sonnet;

namespace {

void registerNothing(runtime::GameRegistry &) {
}

} // namespace

TEST_CASE("a binary opens what needs its own game module or none, and refuses the rest", "[runtime][module]") {
  const runtime::GameModule hook{.name = "hook", .registerGame = &registerNothing};
  const runtime::GameModule other{.name = "other", .registerGame = &registerNothing};

  // Nothing needed: the generic binary and a module's binary both open it.
  REQUIRE(runtime::checkGameModule("project \"Basic\"", "", nullptr, "editor").has_value());
  REQUIRE(runtime::checkGameModule("project \"Basic\"", "", &hook, "editor").has_value());
  // The module it was linked with.
  REQUIRE(runtime::checkGameModule("project \"Hook\"", "hook", &hook, "player").has_value());

  // Without one, or with another, the error names both sides.
  const auto generic = runtime::checkGameModule("project \"Hook\"", "hook", nullptr, "editor");
  REQUIRE(!generic.has_value());
  REQUIRE(generic.error().message ==
          "project \"Hook\" needs the game module \"hook\"; this editor was built without one");
  const auto mismatched = runtime::checkGameModule("bundle \"Hook\"", "hook", &other, "player");
  REQUIRE(!mismatched.has_value());
  REQUIRE(mismatched.error().message ==
          "bundle \"Hook\" needs the game module \"hook\"; this player was built with \"other\"");
}
