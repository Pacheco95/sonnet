# ADR-0043: Rotating supports

- **Status:** Accepted
- **Amends:** [ADR-0038](0038-character-controller.md)
- **Date:** 2026-11-18

## Context

[ADR-0038](0038-character-controller.md) gave characters translating supports and left rotating ones out of scope ([#499](https://github.com/Pacheco95/sonnet/issues/499)). A character on a kinematic body that turns about +Y should hold its place on it at any radius, walk on it with camera-relative intent, leave it with the right velocity when it jumps or steps off, and be able to turn with it. The character sample gets a turntable.

What the code does today:

- `stepCharacter` (`JoltPhysicsWorld.cpp`) calls `CharacterVirtual::UpdateGroundVelocity()` before `ExtendedUpdate` and, while grounded, inherits the horizontal part of `GetGroundVelocity()`.
- Jolt 5.6 builds that velocity in `CalculateCharacterGroundVelocity`, inside `UpdateSupportingContact`'s average. From the support's linear and angular velocity it returns the **chord** velocity: where one step of the support's rotation about its centre of mass, plus its translation, carries the character's point, divided by the step. It does not use the tangent ω×r, so a character carried by it stays at its radius instead of spiralling out by the arc error.
- `MoveKinematic` in step (2) sets a kinematic body's angular velocity from its rotation delta before the characters move in step (3), so `UpdateGroundVelocity` sees this step's spin. `mLastDeltaTime` starts at 1/60, the same as `World::fixedDelta`, so the first step is consistent too.
- The character's rotation is constant between teleports ([ADR-0038](0038-character-controller.md#the-component)); visual facing is a child's rotation that the script owns.
- [PR #570](https://github.com/Pacheco95/sonnet/pull/570) ([#541](https://github.com/Pacheco95/sonnet/issues/541)) draws kinematic bodies between their stepped poses, as characters are, so a character and its moving or turning platform line up as drawn.

So holding a place, walking and the departure velocity need no new physics code, only tests that prove them. What is missing is a way for scripts to see the spin, a `supportVelocity` that matches the velocity the character was carried with (decision 5), the facing rule, the documentation and the sample.

## Decision

Rotation about +Y, with or without a translation, about an axis that need not pass through the character, is supported and tested. Tilting or rolling supports are out of scope.

1. **Positional support.** The character inherits Jolt's chord ground velocity unchanged, with no correction in the wrapper. It is exact for a constant spin at a fixed step, and ADR-0038's velocity rules already treat any support velocity the same way.
2. **Walking.** The horizontal velocity stays the intent plus the inherited support velocity, with the intent in world space. The intent is camera-relative, and the camera does not turn with the platform.
3. **Departure.** A jump keeps the horizontal chord velocity of the last grounded step (the finite-step rotation displacement divided by the step, which tends to ω×r as the step tends to zero), unchanged in the air and replaced on landing: ADR-0038's translating rule. No centrifugal or Coriolis term is added, which is right in the world frame.
   - A walk off the edge ends with a step or two on the support's rounded edge, sliding, where ADR-0038's sliding rule already takes that ground's own velocity. Jolt computes it at the contact point (`c.mLinearVelocity`) rather than at the character, since only a supporting contact gets the chord. On a translating support the two are equal; on a turning one they differ by the spin times the capsule's reach past the edge. The character leaves with the edge's velocity, measured 0.29 m/s from the last grounded chord at 1 rad/s and 3 m, after two sliding steps. ADR-0038's sliding rule is kept: correcting a sliding contact on a kinematic body towards the chord at the character would be a wrapper-side rule of its own, for a difference a player does not see in a fall.
4. **Facing.** The engine does not rotate the controller: turning the root would fight a script's world-space facing, and ADR-0038 keeps the root's rotation constant between teleports.
   - `CharacterState` gains `supportAngularVelocity`: the support body's angular velocity, world space, rad/s, while grounded; zero in the air and while sliding. Lua's `physics.character` table gets the same field.
   - A script that wants inherited facing adds its +Y part times the step to its facing while grounded. The character sample does so behind a property, `turnWithSupport` (default true).
   - The follow camera does not inherit the spin.
5. **`supportVelocity`**, while grounded on a support it already stood on, is the ground velocity before the update: the one the step carried the character with. On the step that lands, and while sliding, it is the ground velocity after the update, as before. The ground velocity after the update is the chord at the character's new position, turned by ω·dt, so it differs from the one that carried it by about ω²r·dt: 0.017 m/s at 1 rad/s and 1 m, but 0.050 m/s at 3 m, which failed the 0.05 m/s bound on the ground speed relative to the support. With the velocity before the update, a character standing on a turning support has a relative ground speed under 1e-5 m/s at every radius tested, and a walking one exactly its intent.
6. **Teleports and outside writes** while supported change nothing: they already clear the inherited velocity, the vertical velocity and the state and refresh the contacts, so the next step picks up the new ground's velocity.
7. **Interpolation.** Characters keep linear interpolation between their steps. At the sample's speeds the chord between two 60 Hz steps is within a millimetre of the arc.

Confirmed by the author:

1. **Aligned drawn poses.** Kinematic bodies are drawn between their stepped poses alongside characters (PR #570), so a character on a turning support is drawn on the same point of it whatever share of a step the frame ends on. This alignment is a contract, tested on rotating supports.
2. **Tilting or rolling supports are out of scope**; only rotation about +Y is claimed.
3. **The sample's turntable stands beside the route**, so the route test is unchanged, and a `capture_turntable` scene shows it.

### Tests

`physics_tests "[character]"`, on a kinematic disc turning at 1 rad/s about +Y:

- Standing at radii 0, 1 and 3 m, the character stays within 1 cm of its point on the disc after 10 s (about 1.6 turns), and its ground speed relative to the support is under 0.05 m/s. The same with the spin combined with a translation.
- 1 m/s of world intent across the disc moves the character at 1 m/s relative to the world plus the chord support velocity.
- A jump leaves with the last grounded step's horizontal chord velocity, kept in the air, compared against the finite-step rotation displacement over the step; its difference from the instantaneous ω×r is reported. A walk off the edge leaves with the velocity of its last supported step, the edge's while sliding, and its difference from the last grounded chord is reported. Landing on static ground replaces either.
- `supportAngularVelocity` is (0, ω, 0) while grounded and zero in the air.
- At 30, 60 and 144 frames per second the drawn position moves monotonically along the arc, within 1 mm of it.
- Teleporting off the disc while supported, and an outside write, carry no spin velocity; teleporting onto it picks the spin up on the next step.

`runtime_tests` runs the character sample with the character on the turntable for a full turn: it stays on it, and with `turnWithSupport` the model's facing relative to the deck stays within 2°.

### Versioning

`CharacterState` (a public header) and the Lua table grow, which is a `feat` and a MINOR bump before 1.0.0. No file format changes.

## Consequences

- Characters ride turntables, carousels and rotating bridges with no game code; a script that wants the character to turn with them adds one line.
- The guarantees rest on Jolt's chord ground velocity; a Jolt update that changes `CalculateCharacterGroundVelocity` is caught by the tests above.
- `docs/physics.md`, `docs/scripting.md` and the character sample's README describe rotating supports, the new field and `turnWithSupport`.
- Supports that tilt or roll, and a camera that turns with the support, are later work if needed.

## Alternatives considered

- **Tangent velocity ω×r at the feet**: what a point velocity gives, but at a fixed step it drifts outwards by the arc error every step, and would need a radial correction in the wrapper.
- **A wrapper-side correction of Jolt's velocity**: unnecessary while Jolt's chord velocity is exact for the spins tested, and a second rule to keep.
- **Rotating the controller with its support**: would turn the root that ADR-0038 keeps constant and fight a script's world-space facing; turning is the script's choice.
- **Adding centrifugal or Coriolis terms on departure**: those are forces of the rotating frame; in the world frame a released point keeps its velocity.
- **Reporting the ground velocity after the update as `supportVelocity`** (the first choice): its ω²r·dt difference reached the 0.05 m/s bound at 3 m.
- **The chord at the character for sliding contacts on kinematic bodies**: would make a walk off a turning edge leave with nearly the last grounded chord, but changes ADR-0038's sliding rule and adds a wrapper-side correction for a difference hidden in a fall.
- **Keeping the drawn lag of a support stepped ahead of its character**: obsolete since PR #570 draws kinematic bodies between their steps.
