# scripting

Gameplay scripts in Lua, behind `IScriptRuntime`, with Lua 5.5 and sol2 as the only implementation ([ADR-0009](decisions/0009-physics-and-scripting.md)). Depends on `physics` publicly, so scripts can drive bodies, and on Lua and sol2 privately. The Lua API below is part of the engine's public API ([conventions.md](conventions.md#versioning)).

| Header | Contents |
|---|---|
| `Components.h` | `Script` and `registerComponents` |
| `ScriptRuntime.h` | `IScriptRuntime`, `ScriptDesc` and `createScriptRuntime` |

## Scripts and instances

A script is a `.lua` file in the project, an asset with an identity like any other ([assets.md](assets.md#importers)). The `Script` component names one by identity, and the entity then runs it in play mode. One script per entity.

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

Every enabled entity with a `Script` gets an instance: a table whose metatable points at the class, holding `self.entity` and whatever the script stores in it. The hooks are optional:

- `start(self)` once, before the instance's first update, in the frame the instance appears.
- `fixedUpdate(self, dt)` every fixed step, after the physics step ([physics.md](physics.md#the-simulation)), with the fixed delta.
- `update(self, dt)` every frame, in the `Update` phase, before physics interpolates what is drawn.

Instances are called in the order of their entities' ids, which is close to creation order. An instance ends when its entity is destroyed or disabled or its `Script` changes to another script. A script's file runs once per load in an environment of its own over the shared globals, so two scripts never overwrite each other's top-level names, while instances of the same script share them.

The runtime's two systems are simulation systems: nothing runs in edit mode. Stopping play in the editor calls `reset`, which drops every instance and every loaded script, so the next play starts from fresh script state as well as from the snapshot. The systems run with the world's deferring suspended, so a script sees its own changes on its next line: a component it just added, the children of a prefab it just instantiated.

## Errors and hot reload

- A script that fails to load (a syntax error, an error at the top level, not returning a table) is reported once per revision of its file, and its entities get no instances.
- A hook that raises an error is reported with the script's file and line as the log record's location, followed by Lua's stack traceback, and turns that instance off, not the others. Log panel links open the script at that line ([editor.md](editor.md#projects-and-scenes)).
- The asset database notices a changed file ([assets.md](assets.md#hot-reload)); the next frame loads it again and swaps the class under the running instances. Their state stays and `start` does not run again; instances that had stopped on an error run again. A revision that fails to load keeps the previous class running.
- `run(code, name)` executes a chunk with the same globals, outside any entity, and returns the error instead of logging it; the tests use it.

Lua is compiled as C++, so a Lua error unwinds C++ frames as an exception and every call into a script is protected. No exception leaves the runtime.

## The Lua API

Scripts get Lua's `base`, `math`, `string`, `table`, `utf8` and `coroutine` libraries, without `dofile` and `loadfile`, and none of `io`, `os`, `package` or `debug`: a script reaches the engine through the tables below only, and runs the same in the player on every platform.

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

The keyboard and mouse as state ([platform.md](platform.md#input-state)), when the runtime was given one; the editor feeds it while playing with the viewport focused ([editor.md](editor.md#play-mode)). Keys and buttons are named as in `platform::Key` and `platform::MouseButton`: `"A"`, `"Digit1"`, `"Space"`, `"LeftShift"`, `"Left"`, `"Right"`. An unknown name raises an error, so a typo does not read as a key never pressed.

| Function | |
|---|---|
| `input.keyDown(key)`, `input.keyPressed(key)`, `input.keyReleased(key)` | Held now; went down or up this frame |
| `input.mouseDown(button)`, `input.mousePressed(button)`, `input.mouseReleased(button)` | The same for mouse buttons |
| `input.mousePosition()`, `input.mouseDelta()`, `input.wheel()` | Tables `{x, y}`: the position relative to the view, this frame's motion and wheel |

Presses and releases last one frame, which may pass without a fixed step: a script that reacts in `fixedUpdate` should note the press in `update`.

### physics

When the runtime was given a physics world ([physics.md](physics.md#queries-and-forces)):

| Function | |
|---|---|
| `physics.raycast(origin, direction, maxDistance, ignore?)` | `{entity, point, normal, distance}` for the nearest hit, or `nil`; `ignore` is an entity whose body is skipped |
| `physics.addImpulse(entity, v)`, `physics.addForce(entity, v)` | For dynamic bodies |
| `physics.linearVelocity(entity)`, `physics.setLinearVelocity(entity, v)` | |
| `physics.angularVelocity(entity)`, `physics.setAngularVelocity(entity, v)` | |

### Sound and animation

`audio` and the animation systems are driven by components, so a script reaches them through `get` and `set` like anything else (ADR-0010): `self.entity:set("AudioSource", { playing = true })` rings a sound that has ended, and `set("Animator", { clip = ..., time = 0 })` switches or restarts a clip. There is no table for either.

### log

`log.debug`, `log.info`, `log.warn` and `log.error` take any values, turn them into strings like `print` and join them with spaces; `print` is `log.info`. Records carry the calling script's file and line ([conventions.md](conventions.md#logging)).

### Maths

`math.random` is Lua's own, which seeds itself differently in every process, so a game's randomness differs run to run. `IScriptRuntime::seedRandom` seeds it as `math.randomseed` would; the editor's captures call it before playing, so a scene that draws random numbers repeats exactly ([editor.md](editor.md#screenshots)).

`vec3(x, y, z)` and `quat(x, y, z, w)` make values with the same metatables component values carry:

- `vec3`: `+`, `-`, unary `-`, `*` and `/` by a number or component-wise, `==`, `tostring`, and the methods `dot`, `cross`, `length`, `normalized` and `lerp(b, t)`.
- `quat`: `q * q` composes, `q * v` rotates a vector, `==`, `tostring`, the methods `inverse` and `normalized`, and the constructors `quat.identity()`, `quat.axisAngle(axis, angle)` and `quat.euler(pitch, yaw, roll)`, which turns by yaw about Y, then pitch about X, then roll about Z. Angles are radians.

## Tests

`scripting_tests` covers the maths and the missing libraries, a seeded `math.random` repeating its draws, errors from `run`, an instance's start and updates in play mode only, an error in `start` reported at the script's line and a reload that fixes it while keeping the instance's state, components through reflection including enums, identities, tags and malformed values, finding, creating, instantiating and destroying entities, input and physics from scripts with the fixed update after the step, log records with the script's location, scripts that fail to load or are missing, and instances following their entities, with `reset` starting the scripts over.
