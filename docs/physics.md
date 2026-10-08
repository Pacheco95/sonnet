# physics

Rigid bodies for the world's entities, behind `IPhysicsWorld`, with Jolt as the only implementation ([ADR-0009](decisions/0009-physics-and-scripting.md)). Depends on `world` publicly and on Jolt privately: no Jolt header appears in the module's public ones.

| Header | Contents |
|---|---|
| `Components.h` | `BodyType`, `RigidBody`, `BoxCollider`, `SphereCollider`, `CapsuleCollider`, `MeshCollider`, the `Trigger` tag, and `registerComponents` |
| `PhysicsWorld.h` | `IPhysicsWorld`, `PhysicsDesc`, `RaycastHit`, `ContactEvent` and `createPhysicsWorld` |

## Components

The components are plain structs registered with the world's reflection ([world.md](world.md#components)), so the inspector edits them, scenes save them and scripts read and write them like the core ones. `createPhysicsWorld` registers them; `registerComponents` does it alone, for tools that load scenes without simulating them, and does nothing the second time.

- An entity with at least one collider is a body. Without a `RigidBody` it is static.
- `Trigger`, a tag, makes the entity's body a sensor: it detects overlaps and pushes nothing ([Events](#events)).
- `RigidBody` gives the body type (static, kinematic or dynamic), the mass in kilograms, friction, restitution, linear and angular damping and a gravity scale.
- `BoxCollider` (half extents), `SphereCollider` (radius) and `CapsuleCollider` (radius and the half height of its cylinder, along Y) each have an offset from the entity's origin. Several colliders on one entity make one compound body.
- `MeshCollider` uses the triangles of a mesh asset for static and kinematic bodies and their convex hull for dynamic ones, since Jolt simulates triangle meshes only when they do not move by themselves. A nil mesh takes the entity's `MeshRenderer` mesh. Every built-in shape takes one. A dynamic body is its convex hull, so a dynamic arch loses its opening, stairs become a ramp and a torus a solid disc (a static or kinematic one keeps every triangle); make those static, or compose the shape from box colliders, when the concavity matters. The vertices come from `AssetDatabase::meshData` ([assets.md](assets.md#meshes)).

Shapes are in the entity's local space and scaled by its world scale, offsets included.

A hierarchy makes compound bodies ([ADR-0022](decisions/0022-gameplay-events-and-script-properties.md)). A collider on an entity with no `RigidBody` of its own belongs to the body of its nearest ancestor that has one, at its pose relative to that ancestor, scale included, so a dumbbell is an empty entity with a `RigidBody` and a box collider on each of two children. The ancestor needs no collider of its own. A child with its own `RigidBody` is a body of its own, and so is a collider with no `RigidBody` anywhere above it, as before. Disabled children, and everything behind a disabled entity, are left out. The body is built again when a folded-in collider is added, removed, disabled, destroyed or moved relative to the root, and a query or a force on a folded-in collider's entity acts on the compound body. A `Trigger` on the root makes the whole compound a sensor and one on a folded-in child is ignored.

## The simulation

`createPhysicsWorld` adds two simulation systems to the world, so both run in play mode only ([world.md](world.md#phases-and-play-mode)):

1. `PhysicsStep`, in `FixedUpdate`, once per fixed step. It destroys the bodies of entities that are gone, disabled or no longer have a collider, creates bodies for the colliders that have none, pushes poses into Jolt, steps Jolt by the fixed delta and writes the dynamic bodies' poses back.
2. `PhysicsInterpolate`, in `PostUpdate`, once per frame, after the scripts' update and before the transform system. It writes each moving dynamic body's pose interpolated between its last two steps by `World::fixedAlpha`, so motion is smooth at any frame rate; what is drawn trails the simulation by up to one step.

The ECS stays the description of the scene. Static and kinematic bodies take their pose from the entity's world transform every step: a static body moved in the editor or by a script is teleported, a kinematic one is moved there with `MoveKinematic` so it pushes dynamic bodies on its way. A dynamic body writes its pose into the local `Transform` (keeping the local scale, and relative to its parent when it has one); when that `Transform` is found different from what physics last wrote, someone else moved it, and the body is teleported to it. A changed world scale rebuilds the body's shape. Setting a `RigidBody` or a collider, from the inspector or a script, destroys the body and the next step builds it again from the new values, which also resets its velocity.

Bodies are created lazily, so edit mode has none. Stopping play reloads the snapshot, whose entities are new, and the bodies of the old ones are destroyed with them. An entity whose collider cannot be built (a missing mesh, a degenerate hull) is reported once and gets no body until one of its physics components changes.

## Queries and forces

`IPhysicsWorld` takes entities, not body ids:

- `raycast(origin, direction, maxDistance, ignore)` returns the nearest hit with its entity, point, surface normal and distance. `ignore` skips one entity's body, which a ray cast from inside it would otherwise hit first.
- `addImpulse` changes a dynamic body's velocity at once, `addForce` acts over the next step; `linearVelocity`, `setLinearVelocity`, `angularVelocity` and `setAngularVelocity` read and write velocities. They act on dynamic bodies only; on anything else they do nothing and report zero.
- An entity that should have a body but has none yet (it got its collider this frame) gets it on the spot, so a script's `start` can push a body the first step has not seen.
- `bodyCount` counts the bodies that exist.
- `debugLines` appends every enabled collider's outline at its entity's world transform: box edges, three great circles per sphere, rings, sides and half circles per capsule, and the triangle edges of a mesh collider. Static colliders are grey, kinematic ones blue, dynamic ones green and darker green while asleep. It reads the components, so it works in edit mode too; the editor draws the lines with the renderer's debug line pass ([rendering.md](rendering.md#the-renderer-module-today)).

## Events

`IPhysicsWorld::events()` returns the `ContactEvent`s of the last fixed step, and the next step replaces them: a kind, the two entities, and for a begin a contact point and normal. The scripting runtime delivers them to scripts ([scripting.md](scripting.md#events)); `physics` itself only records them.

- `ContactBegin` and `ContactEnd` are for solid bodies, `TriggerEnter` and `TriggerExit` for a pair with a `Trigger` in it. A pair is reported once when it starts touching or overlapping and once when it stops, however many shape pairs of a compound body are involved: the events come from how many shape pairs touched before and after the step, not from each of Jolt's callbacks. There is no persistent event; a script that wants a continuing contact keeps its own state between begin and end.
- A body that falls asleep keeps its contacts. Jolt reports the contacts of a sleeping body as removed, and a static sensor sees only awake bodies, so a ball that comes to rest on the ground or inside a trigger would otherwise end its contact; the pair is kept as dormant instead and ends when a body is gone, or when both bodies are awake and no contact came back.
- The pair is ordered by entity id, `first` the lower, and the normal points from `first` towards `second`. The events of a step are sorted by id, so the order does not depend on how the workers were scheduled: `physics_tests` plays a pile of boxes through a trigger with no workers and with six and gets the same events step for step.
- Jolt calls its listener from worker threads ([ADR-0013](decisions/0013-job-system.md)), so the listener only appends a record to a mutex-guarded buffer, which the stepping thread sorts and turns into events after `Update` returns. Records name bodies by id, and a body destroyed between steps keeps its entry until the next step has reported its contacts, which is when a destroyed entity's `ContactEnd` reaches the one it was touching.
- At most `MaxContactEvents` (1024) are kept per step; `droppedEvents()` says how many were not, and the first time it happens a warning is logged.
- Entities in an event may have been destroyed or disabled since; consumers check.

A trigger sees dynamic and kinematic bodies, and static ones only while they move. Jolt's sensors can be made to see static bodies but only from an active kinematic sensor, at a cost that is wasted on the usual pickup.

## Jolt

- Two object layers, moving and non-moving, each with a broad-phase layer of its own; static bodies only meet moving ones.
- Jolt's process-wide state (allocator hooks, factory, type registry) is set up by the first physics world and released by the last. Its trace output goes to the `physics` logger at `debug`, its assertions, in Debug builds, at `error`.
- Jolt's jobs run on the engine's job system ([ADR-0013](decisions/0013-job-system.md)): `JoltJobSystem` derives from Jolt's `JobSystemWithBarrier`, so barriers and dependency counting stay Jolt's, and queues each job onto `core::JobSystem`. `createPhysicsWorld` takes the pool, which has to outlive the world. A barrier's waiter can run a queued job before the worker call that queued it starts, so a step can return with worker calls still to release their jobs into Jolt's free list; `JoltJobSystem`'s destructor waits for them, or the world's teardown would free that list under them. A pool with no workers steps on the calling thread, the way `JobSystemSingleThreaded` did, because Jolt's barrier runs the jobs itself when it waits. The fixed phase holds nothing else, so a step has the whole pool: `physics_tests "[benchmark]"` settles a pile of about a thousand boxes 2.3 times faster on fifteen workers than on none.
- Resting contacts sink by Jolt's penetration slop, 2 cm by default, so a ball of radius 0.5 rests with its centre near 0.49.
- vcpkg builds Jolt with AVX2 and the instructions that come with it on x64, so the editor and player need a CPU from 2013 or later there. Jolt's compile flags are an interface property of its target and reach only the module's own sources, since `physics` links it privately. The manifest asks for the port's `rtti` feature, which vcpkg leaves off by default. Without it, a class deriving from a Jolt base whose virtuals live in the library, as `JoltJobSystem` does, fails to link for want of the base's typeinfo. The module was compiled without RTTI to match, until the undefined-behaviour sanitizer was made fatal. Its objects then had vtables without typeinfo, which the `vptr` check rejects in every other module: `physics_tests` on a `flecs::term` whose vtable the linker took from `physics`, and `scripting_tests`, `runtime_tests` and `editor_tests` when they destroyed the `IPhysicsWorld`. Built with RTTI, Jolt needs no exception in the module's flags.
- The thread sanitizer cannot see Jolt: vcpkg's library is not instrumented, so the ordering Jolt establishes through job dependencies is invisible and reported as races. `tools/tsan.supp` suppresses them and says what covers the module instead ([build.md](build.md#presets)).

## Tests

`physics_tests` covers the registration and scene round trip of the components, a ball falling onto the ground in play mode only, the interpolation between steps, a teleport by an outside write, a kinematic paddle pushing a floating ball, raycasts with their point, normal, distance and ignored entity, impulses and velocities on dynamic bodies only, bodies following their entity's lifetime, colliders and `Disabled`, scaled, offset, compound and mesh shapes including a dynamic convex hull, a collider that cannot be built, the debug lines in edit mode, and a snapshot reload after play, trigger enter and exit without a push, a trigger a body falls asleep inside, a contact's point and normal, sleeping bodies keeping their contacts, a destroyed entity's end, the event order under zero and six workers, the cap, and compound bodies from children (resting level, separate bodies, changes while running, contacts and triggers by body). `physics_tests "[benchmark]"`, hidden by default, measures a stress pile stepping with and without workers.
