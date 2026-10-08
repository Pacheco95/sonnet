#include <sonnet/editor/CommandStack.h>
#include <sonnet/editor/EntityCommands.h>
#include <sonnet/editor/ScriptSlots.h>

#include <sonnet/scripting/ScriptRuntime.h>
#include <sonnet/world/World.h>

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace sonnet;

namespace {

scripting::Scripts three(const core::Uuid &a, const core::Uuid &b, const core::Uuid &c) {
  return {.slots = {{.script = a}, {.script = b}, {.script = c}}};
}

std::vector<core::Uuid> scriptsOf(const scripting::Scripts &scripts) {
  std::vector<core::Uuid> result;
  for (const scripting::ScriptSlot &slot : scripts.slots) {
    result.push_back(slot.script);
  }
  return result;
}

const scripting::PropertyDecl Speed{
    .name = "speed", .type = scripting::PropertyType::Number, .defaultValue = 2.0, .min = 0.0, .max = 10.0};
const scripting::PropertyDecl Target{.name = "target", .type = scripting::PropertyType::Entity};

} // namespace

TEST_CASE("slots are appended, removed and moved as one list", "[editor][scripts]") {
  const core::Uuid a = core::Uuid::generate();
  const core::Uuid b = core::Uuid::generate();
  const core::Uuid c = core::Uuid::generate();
  using List = std::vector<core::Uuid>;
  REQUIRE(scriptsOf(editor::appendSlot({}, a)) == List{a});
  REQUIRE(scriptsOf(editor::appendSlot(three(a, b, c), a)) == List{a, b, c, a});
  REQUIRE(scriptsOf(editor::removeSlot(three(a, b, c), 1)) == List{a, c});
  REQUIRE(scriptsOf(editor::removeSlot(three(a, b, c), 3)) == List{a, b, c});
  // Moving takes the slot to its new place and shifts the ones it passes.
  REQUIRE(scriptsOf(editor::moveSlot(three(a, b, c), 0, 2)) == List{b, c, a});
  REQUIRE(scriptsOf(editor::moveSlot(three(a, b, c), 2, 0)) == List{c, a, b});
  REQUIRE(scriptsOf(editor::moveSlot(three(a, b, c), 1, 2)) == List{a, c, b});
  REQUIRE(scriptsOf(editor::moveSlot(three(a, b, c), 1, 1)) == List{a, b, c});
  REQUIRE(scriptsOf(editor::moveSlot(three(a, b, c), 0, 3)) == List{a, b, c});
  // A moved slot keeps its properties.
  scripting::Scripts withValues = three(a, b, c);
  withValues.slots[0].properties = R"({"speed":5})";
  REQUIRE(editor::moveSlot(withValues, 0, 2).slots[2].properties == R"({"speed":5})");
}

TEST_CASE("a slot stores only the properties that differ from the default", "[editor][scripts]") {
  scripting::ScriptSlot slot{.script = core::Uuid::generate()};
  REQUIRE(editor::propertyValue(slot, Speed) == 2.0);
  REQUIRE(!editor::propertyChanged(slot, Speed));

  slot = editor::setProperty(slot, Speed, 7.5);
  REQUIRE(editor::propertyChanged(slot, Speed));
  REQUIRE(editor::propertyValue(slot, Speed) == 7.5);
  REQUIRE(slot.properties == R"({"speed":7.5})");

  slot = editor::setProperty(slot, Target, "5a0c2f5e-0002-4a5b-8c9d-0000000000aa");
  REQUIRE(slot.properties == R"({"speed":7.5,"target":"5a0c2f5e-0002-4a5b-8c9d-0000000000aa"})");

  // Setting the default again, or reverting, removes the entry, and nothing left is no text.
  REQUIRE(editor::setProperty(slot, Speed, 2.0).properties == R"({"target":"5a0c2f5e-0002-4a5b-8c9d-0000000000aa"})");
  slot = editor::revertProperty(slot, Speed);
  slot = editor::revertProperty(slot, Target);
  REQUIRE(slot.properties.empty());
  REQUIRE(editor::revertProperty(slot, Speed).properties.empty());

  // A name the class no longer declares stays through every edit, and is listed.
  slot.properties = R"({"gone":1,"speed":4})";
  slot = editor::setProperty(slot, Speed, 5.0);
  REQUIRE(slot.properties == R"({"gone":1,"speed":5.0})");
  REQUIRE(editor::undeclaredProperties(slot, {Speed}) == std::vector<std::string>{"gone"});
  REQUIRE(editor::undeclaredProperties(slot, {Speed, {.name = "gone"}}).empty());
  // Text that is not an object is treated as no changes rather than propagated.
  slot.properties = "[1,2]";
  REQUIRE(editor::propertyValue(slot, Speed) == 2.0);
}

TEST_CASE("dropping a script on an entity is one undoable step", "[editor][scripts][commands]") {
  world::World world;
  scripting::registerComponents(world);
  const flecs::entity entity = world.createEntity("Door");
  const core::Uuid uuid = world.uuidOf(entity);
  const core::Uuid first = core::Uuid::generate();
  const core::Uuid second = core::Uuid::generate();
  editor::CommandStack commands;

  REQUIRE(editor::appendScriptCommand(world, core::Uuid::generate(), first) == nullptr);
  commands.push(editor::appendScriptCommand(world, uuid, first), world);
  REQUIRE(entity.get<scripting::Scripts>().slots.size() == 1);
  REQUIRE(entity.get<scripting::Scripts>().slots[0].script == first);
  commands.push(editor::appendScriptCommand(world, uuid, second), world);
  REQUIRE(scriptsOf(entity.get<scripting::Scripts>()) == (std::vector<core::Uuid>{first, second}));

  // The component's own edits are commands too: a property edited and undone.
  const world::ComponentInfo *info = world.findComponent("Scripts");
  const nlohmann::json before = world.componentToJson(entity, info->id);
  scripting::Scripts edited = entity.get<scripting::Scripts>();
  edited.slots[1] = editor::setProperty(edited.slots[1], Speed, 9.0);
  commands.push(editor::componentCommand(uuid, "Scripts", std::make_optional(before),
                                         std::make_optional(world.valueToJson(info->id, &edited)), "edit Scripts"),
                world);
  REQUIRE(entity.get<scripting::Scripts>().slots[1].properties == R"({"speed":9.0})");

  REQUIRE(commands.undo(world));
  REQUIRE(entity.get<scripting::Scripts>().slots[1].properties.empty());
  REQUIRE(commands.undo(world));
  REQUIRE(entity.get<scripting::Scripts>().slots.size() == 1);
  REQUIRE(commands.undo(world));
  REQUIRE(!entity.has<scripting::Scripts>()); // the first drop added the component
  REQUIRE(commands.redo(world));
  REQUIRE(entity.get<scripting::Scripts>().slots[0].script == first);
}
