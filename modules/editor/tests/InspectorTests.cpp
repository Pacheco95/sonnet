#include <sonnet/editor/InspectorPanel.h>

#include <sonnet/world/World.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string_view>

using namespace sonnet;
using Widget = editor::InspectorPanel::ScalarWidget;

namespace {

struct Scalars {
  double precise{0.0};
  std::int32_t count{0};
  std::uint32_t mask{0};
  std::int64_t big{0};
  std::uint64_t bigger{0};
  std::int16_t small{0};
};

const ecs_member_t &memberOf(const world::World &world, flecs::entity_t type, std::string_view name) {
  const flecs::entity entity{world.ecs(), type};
  const flecs::Struct &layout = entity.get<flecs::Struct>();
  const auto *members = ecs_vec_first_t(&layout.members, ecs_member_t);
  for (std::int32_t i = 0; i < ecs_vec_count(&layout.members); ++i) {
    if (name == members[i].name) {
      return members[i];
    }
  }
  FAIL("no member " << name);
  return members[0];
}

} // namespace

TEST_CASE("scalar members get the widget of their reflected kind", "[editor][inspector]") {
  world::World world;
  const flecs::world &ecs = world.ecs();
  const auto widget = [&](flecs::entity_t type, std::string_view member) {
    return editor::InspectorPanel::scalarWidget(ecs, memberOf(world, type, member));
  };
  REQUIRE(widget(ecs.id<world::MeshRenderer>(), "visible") == Widget::Checkbox);
  REQUIRE(widget(ecs.id<world::PointLight>(), "intensity") == Widget::Float);
  REQUIRE(widget(ecs.id<world::Camera>(), "fovY") == Widget::Degrees);
  REQUIRE(widget(ecs.id<world::Camera>(), "nearPlane") == Widget::Float);
  // Not primitives: drawn by the struct, vector and asset branches.
  REQUIRE(widget(ecs.id<world::Transform>(), "position") == Widget::Unsupported);
  REQUIRE(widget(ecs.id<world::MeshRenderer>(), "mesh") == Widget::Unsupported);

  world.ecs()
      .component<Scalars>("Scalars")
      .member<double>("precise")
      .member<std::int32_t>("count")
      .member<std::uint32_t>("mask")
      .member<std::int64_t>("big")
      .member<std::uint64_t>("bigger")
      .member<std::int16_t>("small");
  const flecs::entity_t scalars = ecs.id<Scalars>();
  REQUIRE(widget(scalars, "precise") == Widget::Double);
  REQUIRE(widget(scalars, "count") == Widget::Int);
  REQUIRE(widget(scalars, "mask") == Widget::UInt);
  REQUIRE(widget(scalars, "big") == Widget::Int64);
  REQUIRE(widget(scalars, "bigger") == Widget::UInt64);
  REQUIRE(widget(scalars, "small") == Widget::Unsupported);
}
