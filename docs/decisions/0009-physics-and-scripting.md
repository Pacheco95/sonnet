# ADR-0009: Physics and scripting as world subsystems

- **Status:** Accepted
- **Date:** 2026-09-19

## Context

M4 adds rigid-body physics with Jolt and gameplay scripts in Lua, the two libraries the architecture already names. Both have to live alongside the flecs world without the world depending on them, both run only in play mode, both need a fixed timestep the world does not have yet, and scripts need to reach physics (impulses, raycasts) as well as every component. The questions left open are where the state lives, who calls what each frame, how scripts see components, and in which order the two modules sit.

## Decision

- **Module order.** `world → physics → scripting`: `scripting` links `physics`, because scripts drive bodies and cast rays, while physics never needs scripts. `audio` follows in M5.
- **Subsystems register into the world.** `World::registerComponent` is public; a subsystem constructed on a `World` registers its components with reflection and its systems in the world's phases, tagged `Simulation`, and removes its systems when destroyed. Components therefore reach the inspector, the scene files and scripts through the one registry, and the application's frame stays `world.progress(dt)`.
- **Fixed timestep in `World`.** `FixedUpdate` systems run in a pipeline of their own that `progress` steps zero or more times per frame at a fixed delta (1/60 s by default) from an accumulator, at most a few steps per frame so a long frame slows the simulation instead of spiralling. `World::fixedAlpha` is the fraction of a step left in the accumulator, for interpolation.
- **Bodies follow the ECS.** An entity with a collider component is a body, static unless a `RigidBody` says otherwise. Jolt bodies are created lazily by the physics step and destroyed when the entity or its components go, so edit mode has none and stopping play, which reloads the snapshot, discards them all. Static and kinematic bodies take their pose from the entity's world transform; dynamic bodies write theirs back into the local `Transform`, interpolated between the last two steps for rendering, and a `Transform` written by anyone else teleports the body. Jolt stays behind `IPhysicsWorld`; none of its headers are public.
- **Physics runs on the main thread** through Jolt's single-threaded job system until the engine has its own job system, which Jolt's will then be implemented on.
- **Scripts are assets and classes.** A `.lua` file is an asset referenced by UUID from a `Script` component. The file returns a table; each entity gets an instance table whose metatable points at it, with `start`, `update(dt)` and `fixedUpdate(dt)` called when present. Component values cross into Lua through flecs reflection, generically, never through hand-written per-component bindings. Hot reload swaps the class under live instances and keeps their state.
- **Lua 5.5 compiled as C++, bound with sol2.** Lua errors then unwind C++ frames as exceptions instead of `longjmp`ing over destructors. A failing call is caught at the call site, logged with the script's file and line, and turns that instance off until its script reloads; the per-frame path still never throws. Scripts get the base, math, string and table libraries only, with no file or OS access, so a project runs the same in the player on every platform.

## Consequences

- Adding a component in a later subsystem (audio sources, animation players) is a registration call; the editor, the scene format and scripting need no change.
- The ECS is the single place a scene is described, so play-mode snapshots, prefabs and undo cover physics and scripts for free. The cost is a sync step each fixed tick and a check of every body's transform for outside writes.
- Scripts pay a reflection walk per component access, which is fine for orchestration (ADR-0007) and would not be for per-entity maths over thousands of entities.
- Scripts reference prefabs and entities by UUID or name, as scenes do; nothing in Lua holds a flecs id across frames.
- The world gains the fixed-step pipeline the architecture's frame description promised, and every simulation system now chooses between a fixed and a per-frame phase.

## Alternatives considered

- **Physics owning its own state and pushing into the ECS**: the usual shape for physics engines, but then play-mode snapshots, undo and the inspector would need a second path for bodies.
- **Hand-written sol2 usertypes per component**: faster and more idiomatic in Lua, but every component would need its binding kept in step with its reflection, which the inspector and scene files already have.
- **LuaJIT or Luau**: LuaJIT's JIT is unavailable on iOS and its language stops at 5.1; Luau brings its own type checker and VM for gains a personal engine does not need yet. Plain Lua compiles everywhere the player runs.
- **Scripting at the same level as physics, bridged by the application**: keeps the modules independent, but every physics query scripts need would be a callback the application installs.
