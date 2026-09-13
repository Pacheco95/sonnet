#include <sonnet/world/Components.h>
#include <sonnet/world/World.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace sonnet;
using Catch::Approx;

namespace {

bool approxEqual(const glm::mat4 &a, const glm::mat4 &b, float epsilon = 1e-4f) {
  for (int c = 0; c < 4; ++c) {
    for (int r = 0; r < 4; ++r) {
      if (std::abs(a[c][r] - b[c][r]) > epsilon) {
        return false;
      }
    }
  }
  return true;
}

} // namespace

TEST_CASE("a transform decomposes back from its matrix", "[world][components]") {
  const world::Transform transform{.position = {1.0f, -2.0f, 3.0f},
                                   .rotation =
                                       glm::angleAxis(glm::radians(40.0f), glm::normalize(glm::vec3{1.0f, 1.0f, 0.0f})),
                                   .scale = {2.0f, 0.5f, 3.0f}};
  const world::Transform back = world::Transform::fromMatrix(transform.matrix());
  REQUIRE(approxEqual(back.matrix(), transform.matrix()));
  REQUIRE(back.scale.x == Approx(2.0f));
  REQUIRE(back.scale.y == Approx(0.5f));
  REQUIRE(back.position.z == Approx(3.0f));
  // A mirrored basis keeps a proper rotation and a negative scale.
  const glm::mat4 mirrored = glm::scale(glm::mat4{1.0f}, {-1.0f, 1.0f, 1.0f});
  REQUIRE(world::Transform::fromMatrix(mirrored).scale.x == Approx(-1.0f));
}

TEST_CASE("components round-trip through flecs JSON by reflection", "[world][components]") {
  world::World world;
  const flecs::entity entity = world.createEntity("thing");
  entity.set<world::Transform>({.position = {1.0f, 2.0f, 3.0f},
                                .rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3{0.0f, 1.0f, 0.0f}),
                                .scale = {1.0f, 2.0f, 1.0f}});
  entity.set<world::MeshRenderer>(
      {.primitive = world::Primitive::Sphere, .color = {0.1f, 0.2f, 0.3f, 1.0f}, .visible = false});
  entity.set<world::Camera>({.fovY = glm::radians(45.0f), .nearPlane = 0.25f});
  entity.add<world::Static>();

  const world::ComponentInfo *transform = world.findComponent("Transform");
  const world::ComponentInfo *mesh = world.findComponent("MeshRenderer");
  const world::ComponentInfo *camera = world.findComponent("Camera");
  const world::ComponentInfo *isStatic = world.findComponent("Static");
  REQUIRE(transform != nullptr);
  REQUIRE(mesh != nullptr);
  REQUIRE(camera != nullptr);
  REQUIRE(isStatic != nullptr);
  REQUIRE(isStatic->tag);
  REQUIRE(world.findComponent("Identity") == nullptr); // structural, not a scene component
  REQUIRE(world.findComponent("Name") == nullptr);

  const nlohmann::json transformJson = world.componentToJson(entity, transform->id);
  REQUIRE(transformJson["position"]["y"].get<float>() == Approx(2.0f));
  REQUIRE(transformJson["rotation"]["w"].get<float>() == Approx(std::cos(glm::radians(45.0f))));
  REQUIRE(transformJson["rotation"]["y"].get<float>() == Approx(std::sin(glm::radians(45.0f))));
  const nlohmann::json meshJson = world.componentToJson(entity, mesh->id);
  REQUIRE(meshJson["primitive"] == "Sphere"); // enum constants by name
  REQUIRE(meshJson["visible"] == false);
  REQUIRE(world.componentToJson(entity, isStatic->id).is_null());
  const world::Transform detached{.position = {9.0f, 8.0f, 7.0f}};
  REQUIRE(world.valueToJson(transform->id, &detached)["position"]["x"].get<float>() == Approx(9.0f));
  REQUIRE(world.componentToJson(entity, world.findComponent("Spin")->id).is_null()); // absent

  const flecs::entity other = world.createEntity("copy");
  world.componentFromJson(other, transform->id, transformJson);
  world.componentFromJson(other, mesh->id, meshJson);
  world.componentFromJson(other, camera->id, world.componentToJson(entity, camera->id));
  world.componentFromJson(other, isStatic->id, nullptr);
  REQUIRE(other.get<world::Transform>().position == glm::vec3{1.0f, 2.0f, 3.0f});
  REQUIRE(other.get<world::Transform>().scale.y == Approx(2.0f));
  REQUIRE(other.get<world::Transform>().rotation.w == Approx(entity.get<world::Transform>().rotation.w));
  REQUIRE(other.get<world::MeshRenderer>().primitive == world::Primitive::Sphere);
  REQUIRE(other.get<world::MeshRenderer>().color.b == Approx(0.3f));
  REQUIRE(!other.get<world::MeshRenderer>().visible);
  REQUIRE(other.get<world::Camera>().fovY == Approx(glm::radians(45.0f)));
  REQUIRE(other.has<world::Static>());

  // A partial value assigns the listed fields and keeps the rest.
  world.componentFromJson(other, mesh->id, nlohmann::json::parse(R"({"visible": true})"));
  REQUIRE(other.get<world::MeshRenderer>().visible);
  REQUIRE(other.get<world::MeshRenderer>().primitive == world::Primitive::Sphere);
}

TEST_CASE("angle members carry the radians unit for the inspector", "[world][components]") {
  world::World world;
  const flecs::entity camera{world.ecs(), world.findComponent("Camera")->id};
  // The inspector walks the struct's member list, which names each member's type and unit.
  const flecs::Struct &layout = camera.get<flecs::Struct>();
  const auto *members = ecs_vec_first_t(&layout.members, ecs_member_t);
  const std::size_t count = static_cast<std::size_t>(ecs_vec_count(&layout.members));
  REQUIRE(count == 2);
  REQUIRE(std::string_view{members[0].name} == "fovY");
  REQUIRE(members[0].unit == world.ecs().id<flecs::units::angle::Radians>());
  REQUIRE(members[0].type == world.ecs().id<float>());
  REQUIRE(std::string_view{members[1].name} == "nearPlane");
  REQUIRE(members[1].unit == 0);
}
