# scripting

Gameplay scripts in Lua, behind `IScriptRuntime`, with Lua 5.5 and sol2 as the only implementation ([ADR-0009](decisions/0009-physics-and-scripting.md)). Depends on `physics` publicly, so scripts can drive bodies, and on Lua and sol2 privately. The Lua API below is part of the engine's public API ([conventions.md](conventions.md#versioning)).

| Header | Contents |
|---|---|
| `Components.h` | `ScriptSlot`, `Scripts` and `registerComponents` |
| `ScriptRuntime.h` | `IScriptRuntime`, `ScriptDesc`, `PropertyType`, `PropertyDecl` and `createScriptRuntime` |

## Scripts and instances

A script is a `.lua` file in the project, an asset with an identity like any other ([assets.md](assets.md#importers)). The `Scripts` component holds a list of slots, each `{script, properties}`: the script's identity and the property values its author changed ([Properties](#properties)). The entity runs every slot in play mode, in slot order, each as an instance of its own, so a door can be a `door.lua` and a `squeak.lua` at once ([ADR-0022](decisions/0022-gameplay-events-and-script-properties.md)). The same script may fill several slots. `Scripts` is a list of structs, which reflection learns through `World::registerVector` ([world.md](world.md#components)); the scene file shows it as an array.

A script returns a table, its class:

```lua
local Mover = { speed = 2 } -- fields are defaults every instance sees

function Mover:start()
  self.home = self.entity:get("Transform").position
end

function Mover:update(dt)
  local transform = self.entity:get("Transform")
  transform.position = transform.position + vec3(self.speed * dt, 0, 0)
  self.entity:set("Transform", transform)
end

return Mover
```

Every slot of an enabled entity gets an instance: a table whose metatable points at the class, holding `self.entity`, `self.slot` (the slot's index, counting from one like a Lua array), the class's declared properties and whatever the script stores in it. The hooks are optional:

- `start(self)` once, before the instance's first update, in the frame the instance appears.
- `fixedUpdate(self, dt)` every fixed step, after the physics step ([physics.md](physics.md#the-simulation)), with the fixed delta.
- `update(self, dt)` every frame, in the `Update` phase, before physics interpolates what is drawn.
- `onContactBegin(self, other, contact)`, `onContactEnd(self, other)`, `onTriggerEnter(self, other)` and `onTriggerExit(self, other)`, before the step's `fixedUpdate` ([Events](#events)).

Instances are called in the order of their entities' ids, which is close to creation order, and an entity's slots in slot order. An instance ends when its entity is destroyed or disabled, its slot is removed or the slot's script changes to another; changing the other properties of a slot keeps the instance ([Properties](#properties)). A script's file runs once per load in an environment of its own over the shared globals, so two scripts never overwrite each other's top-level names, while instances of the same script share them.

The runtime's two systems are simulation systems: nothing runs in edit mode. Stopping play in the editor calls `reset`, which drops every instance and every loaded script, so the next play starts from fresh script state as well as from the snapshot. The systems run with the world's deferring suspended, so a script sees its own changes on its next line: a component it just added, the children of a prefab it just instantiated.

## Events

Physics records what touched what during a fixed step and the scripting runtime delivers it ([physics.md](physics.md#events)): at the start of each fixed update, after the step and before any `fixedUpdate` hook, each event is called on the instances of both of its entities, in slot order, first on the entity with the lower id:

| Hook | When |
|---|---|
| `onContactBegin(self, other, contact)` | Two solid bodies started touching. `contact` is `{point, normal}`, `vec3`s: a world-space point on the surface, and the direction from `self`'s entity towards `other` |
| `onContactEnd(self, other)` | They stopped touching |
| `onTriggerEnter(self, other)` | A body started overlapping an entity with the `Trigger` tag ([physics.md](physics.md#components)); both the trigger's scripts and the body's hear it |
| `onTriggerExit(self, other)` | It stopped overlapping |
| `onAnimationEvent(self, name, argument)` | An `Animator` on the instance's entity crossed one of its clip's events ([world.md](world.md#animation)); delivered at the start of the `update` frame, in slot order |

`other` is an `Entity`. A pair is reported once however many shapes touch, a resting body that falls asleep keeps its contacts, and the order within a step is the same whatever Jolt's threads did. An entity that was destroyed or disabled since the step, including by an earlier hook of the same step, gets nothing, but `other` can be one that is gone: `other:isValid()` says. The hooks run on the main thread like every other: Jolt's listener only records. A pickup is a trigger and a few lines:

```lua
local Pickup = { properties = { value = { type = "integer", default = 1, min = 1 } } }

function Pickup:onTriggerEnter(other)
  if other:name() == "Player" then
    log.info("picked up", self.value)
    self.entity:destroy()
  end
end

return Pickup
```

## Properties

A class declares the values an author may change per entity in a `properties` table. The inspector shows one widget per entry and a scene stores the values that differ from the default, so the same script serves a slow door and a fast one:

```lua
local Mover = {
  properties = {
    speed = { type = "number", default = 2, min = 0, max = 10 },
    target = { type = "entity" },
    tint = { type = "color", default = { r = 1, g = 0.5, b = 0.2 } },
  },
}
```

| `type` | In Lua | In the slot's JSON | `default` |
|---|---|---|---|
| `number`, `integer` | number, integer | number | a number (0) |
| `boolean`, `string` | boolean, string | boolean, string | `false`, `""` |
| `vec3` | a `vec3` | `{x, y, z}` | a table with `x`, `y` and `z` (zero) |
| `color` | `{r, g, b, a}` | the same | a table; `a` and the missing channels default to 1 |
| `entity` | an `Entity`, or `nil` when none or not found | the entity's identity | none |
| `asset` | the asset's identity string, or `nil` | the identity | none |

`min` and `max` bound the inspector's widgets for the numeric types; the runtime does not clamp what a file holds. The entries are shown sorted by name, since a Lua table has no order. A slot's `properties` is the JSON object of the values the author changed, by name, and an instance gets the class's defaults overlaid with those values as fields of `self` before `start`. Fields of the class outside `properties` stay plain defaults every instance shares. A value of the wrong kind falls back to the default with a warning, and a name the class no longer declares is kept in the scene and reported, so reverting a script does not lose the data. A malformed entry in the table is reported and left out, once per revision of the file.

`IScriptRuntime::properties(script)` returns a class's declarations, loading the class through the same loader play mode uses, in edit mode too: the file's top-level code runs when the inspector first asks, as it would at the start of play. A class that has not loaded gives an error with the reason. Editing a slot's `properties` while the game runs sets again only the properties whose value changed, on the running instance, so what a script did to the others stays; the editor's snapshot restore discards it all on stop ([editor.md](editor.md#play-mode)).

## require

`require("name")` inside a script loads another script asset as a module and returns what its file returns (`true` when it returns nothing). The name is the file's name without `.lua`, or its path from the project's root or from one of its asset roots (`"scripts/util"`, with or without `.lua`); a path beats a bare name, and a name two files share is an error that says to use the path. The module runs once, in an environment of its own over the shared globals, and the value is shared by every script that asks; it is not a class, takes no instance and no properties. `package` stays absent: `require` is the only way into another file, and only into the project's scripts.

Modules are cached by revision. Changing a module's file loads it again, and every script that required it, directly or through another module, is reloaded through the same hot reload ([below](#errors-and-hot-reload)): its running instances keep their state and take the new class. A `require` cycle raises an error naming the chain (`require cycle: assets/x.lua -> assets/y.lua -> assets/x.lua`); a module that fails to load raises its error in the requiring script, with the module's file and line, and is tried again when its file changes.

## Errors and hot reload

- A script that fails to load (a syntax error, an error at the top level, not returning a table) is reported once per revision of its file, and its entities get no instances.
- A hook that raises an error is reported with the script's file and line as the log record's location, followed by Lua's stack traceback, and turns that instance off, not the others. Log panel links open the script at that line ([editor.md](editor.md#projects-and-scenes)).
- The asset database notices a changed file ([assets.md](assets.md#hot-reload)); the next frame loads it again, and likewise any script that required it, and swaps the class under the running instances. Their state stays and `start` does not run again; instances that had stopped on an error run again. A revision that fails to load keeps the previous class running.
- `run(code, name)` executes a chunk with the same globals, outside any entity, and returns the error instead of logging it; the tests use it.

Lua is compiled as C++, so a Lua error unwinds C++ frames as an exception and every call into a script is protected. No exception leaves the runtime.

## The Lua API

Scripts get Lua's `base`, `math`, `string`, `table`, `utf8` and `coroutine` libraries, without `dofile` and `loadfile`, and none of `io`, `os`, `package` or `debug`: a script reaches the engine through the tables below only, and `require` for the project's other scripts, and runs the same in the player on every platform.

### Components

Components are read and written through reflection, generically ([world.md](world.md#components)): every component registered with the world is reachable by its scene-file name, including those of `physics` and `scripting`. Values map to Lua as follows:

| Reflected type | In Lua |
|---|---|
| `float`, `double` | number |
| integers | integer |
| `bool` | boolean |
| enum | the constant's name as a string (`"Dynamic"`); writing accepts the name or its integer value |
| asset identity (`core::Uuid`) | the canonical string, or `nil` for none |
| `vec3`, `quat` | tables `{x, y, z}` and `{x, y, z, w}` with the `vec3` and `quat` metatables below |
| other structs, `vec4` | tables of their members |
| repeated members, such as `Scripts`' `slots` | not visible yet: `get` leaves them `nil` |

`get` returns a copy; `set` writes the fields present in the table and leaves the others, so `entity:set("RigidBody", { mass = 3 })` changes the mass only. A value of the wrong shape raises an error naming the member. Tags read as `true` when present.

### Entities

`self.entity`, and every entity the API hands out, is an `Entity`. It holds the entity's id with its generation, so an entity that was destroyed raises "the entity no longer exists" rather than reaching whatever reuses its slot.

| Method | |
|---|---|
| `isValid()` | Whether the entity still exists |
| `name()`, `uuid()` | Its name and its identity as a string |
| `get(component)`, `set(component, table)` | A component's value, or `nil` when absent; assigns fields, adding the component when absent |
| `has(component)`, `add(component)`, `remove(component)` | Presence, adding with default values, removal |
| `parent()`, `children()` | The parent or `nil`; an array of the children |
| `worldPosition()` | A `vec3` from the hierarchy, current even before the transform system has run |
| `destroy()` | Destroys it and its children |

Entities compare equal when they are the same entity. An unknown component name raises an error.

### world

| Function | |
|---|---|
| `world.find(uuid)` | The scene entity with that identity, or `nil` |
| `world.findByName(name)` | The first scene entity with that name, or `nil` |
| `world.create(name, parent?)` | A new scene entity with an identity, a name and a transform |
| `world.instantiate(prefab, name?, parent?)` | An instance of a prefab named by identity or by name; an unknown prefab raises an error |

### input

The keyboard, mouse and touches as state ([platform.md](platform.md#input-state)), when the runtime was given one; the editor feeds it while playing with the viewport focused ([editor.md](editor.md#play-mode)). Keys and buttons are named as in `platform::Key` and `platform::MouseButton`: `"A"`, `"Digit1"`, `"Space"`, `"LeftShift"`, `"Left"`, `"Right"`. An unknown name raises an error, so a typo does not read as a key never pressed.

| Function | |
|---|---|
| `input.keyDown(key)`, `input.keyPressed(key)`, `input.keyReleased(key)` | Held now; went down or up this frame |
| `input.mouseDown(button)`, `input.mousePressed(button)`, `input.mouseReleased(button)` | The same for mouse buttons |
| `input.mousePosition()`, `input.mouseDelta()`, `input.wheel()` | Tables `{x, y}`: the position relative to the view, this frame's motion and wheel |
| `input.touches()` | An array of the fingers down now, in the order they went down, each `{id, position, delta}`: an integer that stays the finger's until it lifts, and tables `{x, y}` for the position relative to the view and this frame's motion, as the mouse's |

Presses and releases last one frame, which may pass without a fixed step: a script that reacts in `fixedUpdate` should note the press in `update`. Touches are state, not edges, so `fixedUpdate` can read them. The first finger is also the left mouse button ([platform.md](platform.md#events)), so a script written against the mouse works on a phone, and a script that reads the touches should not read the left button as well.

### camera

The view the game is drawn into, when the runtime was given one: the player's scene camera over its window, or in the editor the scene's camera over the Game panel's image while playing with it on screen, and the viewport's camera over the viewport image otherwise ([editor.md](editor.md#play-mode)). The application hands it over every frame, before the scripts run.

| Function | |
|---|---|
| `camera.ray(point)` | `{origin, direction}`, `vec3`s: the ray from the camera through a point of the view, a table `{x, y}` in the coordinates of `input.mousePosition()` and a touch's `position`. `direction` is unit length |

With `physics.raycast` it finds what is under a pointer or a finger; the basic sample's `player.lua` rolls its ball towards the point on the ground under a held finger that way:

```lua
local touch = input.touches()[1]
if touch then
  local ray = camera.ray(touch.position)
  local hit = physics.raycast(ray.origin, ray.direction, 100, self.entity)
end
```

The ray is the one the editor's gizmo drags along (`renderer::rayDirection`, [rendering.md](rendering.md#the-renderer-module-today)).

### physics

When the runtime was given a physics world ([physics.md](physics.md#queries-and-forces)):

| Function | |
|---|---|
| `physics.raycast(origin, direction, maxDistance, ignore?)` | `{entity, point, normal, distance}` for the nearest hit, or `nil`; `ignore` is an entity whose body is skipped |
| `physics.addImpulse(entity, v)`, `physics.addForce(entity, v)` | For dynamic bodies |
| `physics.linearVelocity(entity)`, `physics.setLinearVelocity(entity, v)` | |
| `physics.angularVelocity(entity)`, `physics.setAngularVelocity(entity, v)` | |

### Sound and animation

`audio` and the animation systems are driven by components, so a script reaches them through `get` and `set` like anything else (ADR-0010): `self.entity:set("AudioSource", { playing = true })` rings a sound that has ended, and `set("Animator", { clip = ..., time = 0 })` switches or restarts a clip; with a `fade` above zero in the same `set`, the clip it replaces fades out over that many seconds instead of cutting. There is no table for either, only the `onAnimationEvent` hook.

### log

`log.debug`, `log.info`, `log.warn` and `log.error` take any values, turn them into strings like `print` and join them with spaces; `print` is `log.info`. Records carry the calling script's file and line ([conventions.md](conventions.md#logging)).

### Maths

`math.random` is Lua's own, which seeds itself differently in every process, so a game's randomness differs run to run. `IScriptRuntime::seedRandom` seeds it as `math.randomseed` would; the editor's and the player's captures call it before playing, so a scene that draws random numbers repeats exactly ([editor.md](editor.md#screenshots), [player.md](player.md#capture-runs)).

`vec3(x, y, z)` and `quat(x, y, z, w)` make values with the same metatables component values carry:

- `vec3`: `+`, `-`, unary `-`, `*` and `/` by a number or component-wise, `==`, `tostring`, and the methods `dot`, `cross`, `length`, `normalized` and `lerp(b, t)`.
- `quat`: `q * q` composes, `q * v` rotates a vector, `==`, `tostring`, the methods `inverse` and `normalized`, and the constructors `quat.identity()`, `quat.axisAngle(axis, angle)` and `quat.euler(pitch, yaw, roll)`, which turns by yaw about Y, then pitch about X, then roll about Z. Angles are radians.

## Tests

`scripting_tests` covers the maths and the missing libraries, several slots on an entity starting and updating in order and following changes to the list, contact and trigger events reaching both entities with the normal from each one's side before the step's hooks and not reaching a destroyed or disabled entity, declared properties of every type with their defaults, overlays, wrong values, undeclared names and live edits, malformed declarations, `require` loading a module once and reloading its users when it changes, its cycles, unknown and ambiguous names and failing modules, `Scripts` through a scene and the migration of a version 2 `Script`, a seeded `math.random` repeating its draws, errors from `run`, an instance's start and updates in play mode only, an error in `start` reported at the script's line and a reload that fixes it while keeping the instance's state, components through reflection including enums, identities, tags and malformed values, finding, creating, instantiating and destroying entities, input and physics from scripts with the fixed update after the step, `input.touches()` with its fields and their types, `camera.ray` through the centre and a corner of a known view, log records with the script's location, scripts that fail to load or are missing, and instances following their entities, with `reset` starting the scripts over.
