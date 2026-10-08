# ADR-0022: Physics events, script properties and several scripts per entity

- **Status:** Accepted
- **Date:** 2026-10-07

## Context

M4 gave scripts bodies to push and rays to cast, but no way to hear that something touched them. A pickup has to poll with raycasts. It also left scripts configurable only by editing the Lua file: a class's fields are defaults every instance shares, the `Script` component holds one identity and nothing else, and an entity can run one script. M11 closes these, and all three change the scene format, which has to settle before 1.0.0.

Constraints found in the code:

- Jolt calls its contact listener from worker threads while the step runs ([ADR-0013](0013-job-system.md)); a script must never be called from one.
- `scripting` links `physics` and not the other way round ([ADR-0009](0009-physics-and-scripting.md)), so events cross upward as data.
- Reflection registers scalars, strings, enums, identities, vectors of floats and nested structs. It has no repeated members, and `Script` is one component per entity because flecs holds one value per component type.
- The scene camera in play mode, which the roadmap listed for M11, already exists: `world::sceneCamera` feeds the player, and the editor's Game panel draws through it. It needs nothing here.

## Decision

**Events are data, recorded by physics and delivered by scripting.**

- `IPhysicsWorld::events()` returns the `ContactEvent`s of the last fixed step: a kind (`ContactBegin`, `ContactEnd`, `TriggerEnter`, `TriggerExit`), the two entities and, for contacts, a point and normal. Jolt's `ContactListener` appends to a mutex-guarded buffer from any thread; after the step, on the calling thread, the buffer is sorted by entity ids so delivery order does not depend on thread timing, capped per step (excess is counted and logged once), and swapped into the list `events()` reads. Entities are flecs ids; consumers check they are alive.
- A new tag component `Trigger` turns an entity's colliders into sensors: they detect overlaps with dynamic, kinematic and other bodies, and push nothing. Enter and Exit are reported once per pair, not each step. Contact events are reported for every pair that begins or ends touching, with no persistent event, because a script wanting a continuing contact keeps its own state between begin and end.
- `scripting` runs the events at the start of each `fixedUpdate` frame, before the hooks, calling on each of the two entities' scripts the hook that fits: `onContactBegin(self, other, contact)`, `onContactEnd(self, other)`, `onTriggerEnter(self, other)` and `onTriggerExit(self, other)`, where `other` is an `Entity` and `contact` is `{point, normal}`. An entity destroyed or disabled since the step gets nothing.
- Compound bodies from a hierarchy: a child with colliders and no `RigidBody` of its own contributes its shapes, at its offset from the parent, to the nearest ancestor's body, which today ignores them. A child with its own `RigidBody` stays a body of its own.

**Several scripts: a `Scripts` component replaces `Script`.**

- `Scripts` holds a list of slots, each `{script, properties}`. Reflection learns repeated members of reflectable structs for it, through flecs' opaque vector support, in `world`, so that audio and animation can use them later. An instance is keyed by entity and slot, and its hooks run in slot order. `self.entity` is shared; `self.slot` is the index.
- The scene format goes to version 3 and migrates each `Script` into a one-slot `Scripts`. The editor's inspector shows the list with add, remove and reorder, and a script asset dropped on an entity appends a slot.
- If vector reflection proves not to survive flecs' serializer and the inspector, the fallback is a fixed `Script` slot count of four, each a member struct; the decision, not the container, is what the scene format freezes.

**Script properties are declared by the class and stored by the slot.**

- A class declares them in a `properties` table: `properties = { speed = { type = "number", default = 2, min = 0, max = 10 }, target = { type = "entity" } }`. Types are `number`, `integer`, `boolean`, `string`, `vec3`, `color`, `entity` (an identity) and `asset` (an identity). Reading the table needs only the loaded class, so the editor evaluates it through the same loader as play mode.
- A slot's `properties` is a JSON object of the values the author changed, by name. An instance gets a copy of the class defaults overlaid with those values before `start`, as fields of `self`. A name the class no longer declares is kept and reported once, so reverting a script does not lose data; a value of the wrong type falls back to the default with a warning.
- The inspector draws one widget per declared property, with a revert button when it differs from the default. Edits are an `ICommand` like any component edit and apply to a running instance's field in play mode, which the snapshot restore discards on stop.
- Fields of the class outside `properties` stay plain shared defaults, as today.

**`require` is for script assets.** `require("name")` inside a script resolves a script asset by its name or its project-relative path, loads it as a class-less module in its own environment, returns what its file returns and caches it per revision. A module is loaded once however many scripts require it. Changing a module's file reloads every script that required it, through the existing hot reload. A cycle raises an error naming the chain. `package` stays absent; `require` is the only entry to it.

## Consequences

- A pickup, a door and a damage zone are scripts with `onTriggerEnter` and a property for their numbers; none of them polls.
- Events add one mutex acquisition per contact, which Jolt serialises on anyway for the listener; the cap bounds a pile of boxes settling. The M8 benchmark runs again to check that the thousand-box step does not slow.
- `Script` leaves the scene format, the Lua `get`/`set` names and the docs. Scripts that call `entity:get("Script")` fail loudly with the unknown component error; the sample and playground scenes migrate through the new version.
- The Lua API grows hooks and `require`, which are public API ([conventions.md](../conventions.md#versioning)); 1.0.0 freezes them.
- Cooked bundles carry scenes as CBOR documents, so version 3 reaches them without a bundle change, and `sonnet_cook` writes it.

## Alternatives considered

- **Callbacks installed by the application, called from the listener**: would run Lua on a Jolt worker and make the order of calls depend on thread timing.
- **A `Collider::isTrigger` flag**: a trigger would need a collider, but the flag belongs to the body as a whole, which is what a tag says; it also keeps the collider structs unchanged.
- **Several scripts as child entities**: no component change, but every extra script is an entity in the hierarchy, in the way of the user and of compound bodies.
- **Properties as reflected members of a per-script generated component**: the inspector would be free, but the scene's schema would depend on script files, which a player without importers cannot check.
- **Properties by scanning the class's plain fields**: no declaration to write, but nothing says a number is an angle or a colour, or that a field is meant to be edited rather than state.
