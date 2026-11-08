# physics

Rigid bodies for the world's entities, behind `IPhysicsWorld`, with Jolt as the only implementation ([ADR-0009](decisions/0009-physics-and-scripting.md)). Depends on `world` publicly and on Jolt privately: no Jolt header appears in the module's public ones.

| Header | Contents |
|---|---|
| `Components.h` | `BodyType`, `RigidBody`, `BoxCollider`, `SphereCollider`, `CapsuleCollider`, `MeshCollider`, the `Trigger` tag, `CharacterController`, and `registerComponents` |
| `PhysicsWorld.h` | `IPhysicsWorld`, `PhysicsDesc`, `RaycastHit`, `SphereCastHit`, `CharacterState`, `ContactEvent` and `createPhysicsWorld` |

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

1. `PhysicsStep`, in `FixedUpdate`, once per fixed step. It destroys the bodies and characters of entities that are gone, disabled (themselves or through an ancestor) or no longer have a collider or controller, creates the missing ones and checks the controllers, pushes poses into Jolt, moves the characters, steps Jolt by the fixed delta and writes the dynamic bodies' and characters' poses back ([Characters](#characters) has the order in full).
2. `PhysicsInterpolate`, in `PostUpdate`, once per frame, after the scripts' update and before the transform system. It writes each moving dynamic body's pose interpolated between its last two steps by `World::fixedAlpha`, and each character's position the same way (its rotation changes only when it is teleported), so motion is smooth at any frame rate; what is drawn trails the simulation by up to one step.

The ECS stays the description of the scene. Static and kinematic bodies take their pose from the entity's world transform every step: a static body moved in the editor or by a script is teleported, a kinematic one is moved there with `MoveKinematic` so it pushes dynamic bodies on its way. A dynamic body writes its pose into the local `Transform` (keeping the local scale, and relative to its parent when it has one); when that `Transform` is found different from what physics last wrote, someone else moved it, and the body is teleported to it. A changed world scale rebuilds the body's shape. Setting a `RigidBody` or a collider, from the inspector or a script, destroys the body and the next step builds it again from the new values, which also resets its velocity.

Bodies are created lazily, so edit mode has none. Stopping play reloads the snapshot, whose entities are new, and the bodies of the old ones are destroyed with them. An entity whose collider cannot be built (a missing mesh, a degenerate hull) is reported once and gets no body until one of its physics components changes.

## Queries and forces

`IPhysicsWorld` takes entities, not body ids:

- `raycast(origin, direction, maxDistance, ignore)` returns the nearest hit with its entity, point, surface normal and distance. `ignore` skips one entity's body, or a character's inner body, which a ray cast from inside it would otherwise hit first.
- `sphereCast(origin, radius, direction, maxDistance, ignore)` moves a sphere's centre from `origin` along `direction` for up to `maxDistance` through Jolt's `NarrowPhaseQuery::CastShape` and returns the first solid body it touches: the entity, the point on that body's surface, the unit normal from the point towards the sphere's centre at the hit, and the distance the centre travelled. It is what a collision-aware camera needs. Its rules:
  - `direction` need not be normalised; the distance is in world units along it.
  - Triggers are never hit, from outside or inside. `ignore` is a span of entities whose bodies are skipped, so a camera skips every body of its character; a collider folded into a compound body skips that whole body, a character controller's entity skips its inner body (all there is of a character in the broad phase), and an entity with no body changes nothing.
  - A sphere that starts out overlapping a body hits it at distance 0, whichever way it moves, with the deepest overlap first and the normal that pushes the sphere out, also from wholly inside a body; back faces of mesh triangles count.
  - A zero direction or a `maxDistance` of 0 tests that overlap alone, where the sphere stands.
  - A radius that is not positive, a negative `maxDistance`, or any value that is not finite hits nothing.
  - Tolerances are Jolt's: distances are good to its collision tolerance, and a box's edges and corners are rounded by its convex radius (up to 5 cm), so a sphere grazing an edge stops a little before a sharp edge would stop it.
- `addImpulse` changes a dynamic body's velocity at once, `addForce` acts over the next step; `linearVelocity`, `setLinearVelocity`, `angularVelocity` and `setAngularVelocity` read and write velocities. They act on dynamic bodies only; on anything else they do nothing and report zero.
- An entity that should have a body but has none yet (it got its collider this frame) gets it on the spot, so a script's `start` can push a body the first step has not seen.
- `bodyCount` counts the rigid bodies that exist; characters and their inner bodies are not counted.
- `debugLines` appends every enabled collider's outline at its entity's world transform: box edges, three great circles per sphere, rings, sides and half circles per capsule, and the triangle edges of a mesh collider. Static colliders are grey, kinematic ones blue, dynamic ones green and darker green while asleep. Every enabled `CharacterController` adds its authored capsule, standing on the origin plus its offset, in orange, whether or not it is rejected. It reads the components, so it works in edit mode too; the editor draws the lines with the renderer's debug line pass ([rendering.md](rendering.md#the-renderer-module-today)).

## Characters

A `CharacterController` makes its entity a character moved by intent rather than by forces ([ADR-0038](decisions/0038-character-controller.md)): a Jolt `CharacterVirtual` that sweeps an upright capsule through the world, climbs steps, follows the ground down and stands on slopes, with no game code for any of it.

| Field | Unit | Default | Valid |
|---|---|---|---|
| `radius` | m | 0.3 | > 0 |
| `halfHeight` | m, of the cylinder | 0.6 (a 1.8 m capsule) | ≥ 0 |
| `offset` | m, entity space | (0, 0, 0) | finite |
| `maxSlope` | radians, shown in degrees | 50° | > 0 and < 90° |
| `stepHeight` | m | 0.3 | 0 to `halfHeight + radius` |
| `groundSnap` | m | 0.3 | 0 to `halfHeight + radius` |
| `mass` | kg | 70 | > 0 |
| `pushStrength` | N | 100 | ≥ 0 |

- **Origin.** The entity's origin is the character's feet: the capsule stands along world +Y with its bottom at the origin plus `offset`, so a model child at the origin stands on the ground. Contacts below the centre of the lower half sphere support it; higher ones only collide. The capsule is never scaled.
- **Rejection.** A value out of range, a `RigidBody`, collider or `Trigger` on the entity, a `CharacterController` or `RigidBody` above it, an enabled collider anywhere below it, a world scale other than 1 or a world rotation that is not a yaw about +Y (each to 1e-3) rejects the controller: it has no character, its commands return false, `character` returns nothing and the reason is logged once. Every step checks again, so the controller comes back as soon as the problem is gone, and a change to one of its physics components logs a remaining problem again. Values are never clamped. The other entities keep their bodies.
- **Lifetime.** Characters are made lazily, by the step or on the spot by a command, in play mode only: edit mode has none, and stopping play discards them with the snapshot's entities. Disabling the entity or an ancestor, removing the controller or destroying the entity removes the character and its inner body at once; enabling it again makes a new one where the entity is. Setting the controller, or any physics component on the entity, rebuilds it at the entity's pose with its transient state (intent, velocity, jump, inherited velocity) cleared.
- **Parents.** A character should be a root. Under a parent it is simulated in world space and its local `Transform` is written relative to the parent, as a dynamic body's is, so a moving parent does not carry it; riding something is what support is for.

### Commands and state

`IPhysicsWorld` drives characters by entity, and Lua's `physics` table has the same calls under the same names ([scripting.md](scripting.md#physics)). Each returns false (or nothing) on an entity that is gone, disabled, in edit mode, has no controller or a rejected one:

- `setCharacterMove(entity, horizontalVelocity)` sets the world-space intent in m/s, held until changed. Its `y` must be 0 (to 1e-4) and every component finite, or the call returns false and keeps the old intent. There is no speed limit.
- `jumpCharacter(entity, verticalSpeed)` is a one-shot consumed by the next fixed step, whatever the ground: the script owns eligibility (a grounded check, coyote time, double jumps). The speed must be finite and positive; of several requests before a step, the last wins.
- `teleportCharacter(entity, position, yaw)` puts the origin at a world position facing `yaw` radians about +Y and writes the entity's local `Transform` at once. It clears the velocity, the intent, a pending jump, the inherited velocity, the support and the interpolation.
- `character(entity)` returns the `CharacterState` the last step left: `velocity` (what the step moved it by, per second, its support's motion included), `grounded` (on walkable ground), `sliding` (on ground too steep to stand on) and, while either holds, `groundNormal`, `support` (the entity stood on, which may since have gone) and `supportVelocity`; they are zero and none in the air. Until a step has moved it, a character is not grounded and at rest.
- A `Transform` written by anyone else (a script, the inspector, a gizmo, an undo) teleports the character there with the same clearing, its rotation's yaw becoming the character's; a tilt rejects it.

### The step

Inside `PhysicsStep`, in order:

1. Stale bodies and characters are destroyed, missing ones created and the controllers checked.
2. Poses in: static bodies, kinematic `MoveKinematic`, outside writes to dynamic bodies and to characters.
3. The characters move, one after another in entity-id order, on the stepping thread: each consumes its commands, builds its velocity and calls `ExtendedUpdate` against the world as the last step left it and with this step's kinematic velocities, so a platform's motion is the character's support velocity. Their pushes on dynamic bodies (impulses through Jolt's body interface) happen here, and they use the step's temporary allocator, both strictly before Jolt's update begins.
4. `PhysicsSystem::Update` on the job system.
5. The contact records become events.
6. Poses out: dynamic bodies and characters.

The wrapper owns the velocity; `ExtendedUpdate` only moves along it, slides, climbs steps (`stepHeight` as its stair step-up) and snaps down (`groundSnap` as its stick-to-floor distance):

- **Standing** on walkable ground with no jump pending, the vertical velocity is the support's and gravity adds nothing, so there is no creep down a walkable slope and no throw when a lift stops. A fall that Jolt already counts as landed, with the ground within its predictive distance, carries on down until the ground stops it.
- **In the air and on steep ground**, the world's gravity (`PhysicsDesc::gravity`, no per-character scale) is added each step and the vertical velocity is kept, except what a ceiling stopped on the way up. A step climbed or a snap down adds nothing to it.
- **Support velocity.** While grounded the support's horizontal velocity is inherited; it is kept unchanged in the air (no air drag) and replaced on landing. On steep ground it becomes that ground's own: nothing on a static slope, the platform's while the capsule slides past the rounded edge of a moving one. The horizontal velocity is the intent plus the inherited velocity, less what pushes up a steep slope.
- **Jump.** A consumed jump replaces the vertical velocity with the support's (when grounded) plus the jump speed, then adds the step's gravity, so a late coyote jump goes as high as one from the ground; it leaves walkable ground on the step that consumes it.

Rotating supports are not handled yet ([#499](https://github.com/Pacheco95/sonnet/issues/499)).

### Presence and contacts

- **Inner body.** Each character has an inner body: a kinematic capsule 0.9 times the radius, standing on the same origin, in the moving layer, never sleeping, with the controller's entity as its user data and in the body-to-entity map. It is what raycasts, sphere casts, triggers and fast dynamic bodies see, and `ignore` naming the controller's entity skips it. The `CharacterVirtual` itself is never in the broad phase, and its own sweeps never meet its inner body.
- **Triggers** are the inner body's: `TriggerEnter` and `TriggerExit` with the controller's entity, through the rigid-body listener and its per-pair counting. The character's own sensor contacts are dropped.
- **Solid contacts** are the character's own: its `CharacterContactListener` records added and removed contacts with solid bodies, on the stepping thread during step 3, into the same records as the rigid-body listener, so `ContactBegin` (landing, walking into a wall or a crate) and `ContactEnd` (a jump, walking away) come with the same ordering and cap. The inner body's solid contacts are dropped, so no pair is reported twice. A character that is destroyed, disabled or rebuilt ends its contacts in the next step's events, and a body destroyed under it ends its contact the same way.
- **Pushing.** With `pushStrength` 100 N a character pushes a 20 kg crate and is stopped by a 1000 kg one.
- **Characters with each other.** There is no character-versus-character collision in Jolt's sense. Each character meets the others through their inner bodies, as kinematic obstacles: two characters walking into each other stop with their origins one radius plus the other's inner radius apart (plus Jolt's padding), neither pushes the other, and the pair begins once even though both report it. An inner body is moved by placing it, with no velocity of its own, so a character standing on another is not carried by it. A crowd, or characters that shove each other, would need Jolt's `CharacterVsCharacterCollision` and a different design.

### Tolerances

The behaviour is Jolt's, with its contact tolerances rather than exact geometry:

- A character rests with its origin within a few millimetres of the floor and stops 2 cm (Jolt's character padding) short of a wall.
- The rounded bottom of the capsule climbs an edge by itself up to `radius (1 - cos maxSlope)`, 0.107 m with the defaults, so the highest step climbed is `stepHeight` plus that. Climbing is spread over a few steps as the capsule rides up the edge, not one jump of the whole height.
- Walking off an edge lower than `groundSnap` keeps the character on the ground except for a step or two while its rounded bottom rolls over the edge, which Jolt sees as steep ground; higher edges are falls.
- What is drawn trails the simulation by up to one fixed step, as for dynamic bodies, and moves by the frame's share of the motion at any frame rate.

## Events

`IPhysicsWorld::events()` returns the `ContactEvent`s of the last fixed step, and the next step replaces them: a kind, the two entities, and for a begin a contact point and normal. The scripting runtime delivers them to scripts ([scripting.md](scripting.md#events)); `physics` itself only records them.

- `ContactBegin` and `ContactEnd` are for solid bodies, `TriggerEnter` and `TriggerExit` for a pair with a `Trigger` in it. A pair is reported once when it starts touching or overlapping and once when it stops, however many shape pairs of a compound body are involved: the events come from how many shape pairs touched before and after the step, not from each of Jolt's callbacks. There is no persistent event; a script that wants a continuing contact keeps its own state between begin and end.
- A body that falls asleep keeps its contacts. Jolt reports the contacts of a sleeping body as removed, and a static sensor sees only awake bodies, so a ball that comes to rest on the ground or inside a trigger would otherwise end its contact; the pair is kept as dormant instead and ends when a body is gone, or when both bodies are awake and no contact came back.
- The pair is ordered by entity id, `first` the lower, and the normal points from `first` towards `second`. The events of a step are sorted by id, so the order does not depend on how the workers were scheduled: `physics_tests` plays a pile of boxes through a trigger with no workers and with six and gets the same events step for step.
- Jolt calls its listener from worker threads ([ADR-0013](decisions/0013-job-system.md)), so the listener only appends a record to a mutex-guarded buffer, which the stepping thread sorts and turns into events after `Update` returns. Records name bodies by id, and a body destroyed between steps keeps its entry until the next step has reported its contacts, which is when a destroyed entity's `ContactEnd` reaches the one it was touching.
- At most `MaxContactEvents` (1024) are kept per step; `droppedEvents()` says how many were not, and the first time it happens a warning is logged.
- Entities in an event may have been destroyed or disabled since; consumers check.

Characters contribute their own solid contacts and their inner bodies' trigger overlaps ([Presence and contacts](#presence-and-contacts)).

A trigger sees dynamic and kinematic bodies, and static ones only while they move. Jolt's sensors can be made to see static bodies but only from an active kinematic sensor, at a cost that is wasted on the usual pickup.

## Jolt

- Two object layers, moving and non-moving, each with a broad-phase layer of its own; static bodies only meet moving ones.
- Jolt's process-wide state (allocator hooks, factory, type registry) is set up by the first physics world and released by the last. Its trace output goes to the `physics` logger at `debug`, its assertions, in Debug builds, at `error`.
- Jolt's jobs run on the engine's job system ([ADR-0013](decisions/0013-job-system.md)): `JoltJobSystem` derives from Jolt's `JobSystemWithBarrier`, so barriers and dependency counting stay Jolt's, and queues each job onto `core::JobSystem`. `createPhysicsWorld` takes the pool, which has to outlive the world. A barrier's waiter can run a queued job before the worker call that queued it starts, so `JoltJobSystem::WaitForJobs` waits for those calls after the barrier finishes. Their references are released before the next step: otherwise completed jobs accumulate in the bounded free list until it is exhausted, which caused the intermittent macOS Release bus error ([diagnosis](reports/macos-physics-job-pool.md)). The destructor waits too, so teardown cannot free the list under a worker. A pool with no workers steps on the calling thread, the way `JobSystemSingleThreaded` did, because Jolt's barrier runs the jobs itself when it waits. The fixed phase holds nothing else, so a step has the whole pool: `physics_tests "[benchmark]"` settles a pile of about a thousand boxes 2.3 times faster on fifteen workers than on none.
- Resting contacts sink by Jolt's penetration slop, 2 cm by default, so a ball of radius 0.5 rests with its centre near 0.49.
- vcpkg builds Jolt with AVX2 and the instructions that come with it on x64, so the editor and player need a CPU from 2013 or later there. Jolt's compile flags are an interface property of its target and reach only the module's own sources, since `physics` links it privately. The manifest asks for the port's `rtti` feature, which vcpkg leaves off by default. Without it, a class deriving from a Jolt base whose virtuals live in the library, as `JoltJobSystem` does, fails to link for want of the base's typeinfo. The module was compiled without RTTI to match, until the undefined-behaviour sanitizer was made fatal. Its objects then had vtables without typeinfo, which the `vptr` check rejects in every other module: `physics_tests` on a `flecs::term` whose vtable the linker took from `physics`, and `scripting_tests`, `runtime_tests` and `editor_tests` when they destroyed the `IPhysicsWorld`. Built with RTTI, Jolt needs no exception in the module's flags.
- The thread sanitizer cannot see Jolt: vcpkg's library is not instrumented, so the ordering Jolt establishes through job dependencies is invisible and reported as races. `tools/tsan.supp` suppresses them and says what covers the module instead ([build.md](build.md#presets)).

## Tests

`physics_tests` covers the registration and scene round trip of the components, a ball falling onto the ground in play mode only, the interpolation between steps, a teleport by an outside write, a kinematic paddle pushing a floating ball, raycasts with their point, normal, distance and ignored entity, sphere casts (open space, a wall at a known distance, a thin wall, edges and corners, initial overlaps and zero-length moves, ignored entities and compound bodies, triggers skipped, invalid input rejected), impulses and velocities on dynamic bodies only, bodies following their entity's lifetime, colliders and `Disabled`, a body under a disabled ancestor leaving at once and coming back where it was, scaled, offset, compound and mesh shapes including a dynamic convex hull, a collider that cannot be built, the debug lines in edit mode, and a snapshot reload after play, trigger enter and exit without a push, a trigger a body falls asleep inside, a contact's point and normal, sleeping bodies keeping their contacts, a destroyed entity's end, the event order under zero and six workers, the cap, and compound bodies from children (resting level, separate bodies, changes while running, contacts and triggers by body). `physics_tests "[character]"` covers the controller through `IPhysicsWorld` in play mode: registration and scene round trip, play-mode-only creation and command input checks, a fall onto flat ground with its contact, diagonal wall sliding and a corner, steps and slopes a margin either side of their limits, standing on a walkable slope and sliding down a steep one, a downhill run that never leaves the ground, snapping down a low ledge and falling off a high one, a jump with its peak, landing and contact events and a ceiling that cuts it short, a translating platform ridden, jumped off and walked off, pushing a light crate and not a heavy one with one contact each and a destroyed crate's end, a trigger's enter and exit and a destroyed character's exit, raycasts and sphere casts hitting the inner body and skipping it when ignored, teleports, outside writes and a tilt, smooth interpolation at 30, 60 and 144 frames per second, zero-step, multi-step and zero-time-scale frames, configuration changes and every rejection, lifetime through destruction, disabled ancestors, controller removal and the play snapshot, a moving parent, two characters blocking each other, and the debug capsule in edit and play mode. The eleven Jolt-only feasibility cases that ADR-0038 was decided on are in the same tag. `physics_tests "[jobs]"` holds a worker back while a barrier executes its job and verifies the queued reference is released before the wait returns, then reuses a one-slot pool across 64 steps. `physics_tests "[benchmark]"`, hidden by default, measures a stress pile stepping with and without workers.
