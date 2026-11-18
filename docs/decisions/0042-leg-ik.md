# ADR-0042: Leg IK as a world-matrix pass before the skin palette, fed by the game's ground query

- **Status:** Accepted
- **Amends:** [ADR-0023](0023-animation-blending-morph-targets-and-particles.md) and [ADR-0038](0038-character-controller.md)
- **Date:** 2026-11-18

## Context

[#500](https://github.com/Pacheco95/sonnet/issues/500) plants the character's feet on steps and slopes. Its refinement settled the design and the author confirmed the remaining questions on 2026-11-08; this ADR, split out as [#613](https://github.com/Pacheco95/sonnet/issues/613), records it so the build can start from an accepted decision.

What the code does today:

- **The pose pipeline** ([ADR-0023](0023-animation-blending-morph-targets-and-particles.md)). `AnimationPlayback` (in `Update`) samples and blends the layers into the joints' local `Transform`s. In `PreRender`, the transform system propagates them into `WorldTransform`s, and `SkinPalette` builds each skin's palette from those. Nothing changes the pose between the blend and the palette.
- **The frame** ([ADR-0038](0038-character-controller.md#the-frame), step 5). `PreRender` runs the previous world transforms, the transform system, the skin palettes, then audio.
- **The sample.** The humanoid's clips are authored for flat ground. On the 0.2 m step edges and the 20° ramp of the character level, one foot floats or sinks into the surface. The capsule's origin is the feet ([ADR-0038](0038-character-controller.md#the-component)), and the model child stands at it.
- **Bones.** The humanoid uses the mannequin names `pelvis`, `thigh_l/r`, `calf_l/r`, `foot_l/r`, `ball_l/r` (`LICENSES.md`, retargeting). The retargeting defined a foot contact as the ankle within 2.5 cm of its bind-pose height.

Two modules meet: `world` poses the model, and the ground under each foot is a physics query that a game filters its own way. Since [ADR-0022](0022-gameplay-events-and-script-properties.md) and ADR-0023, data goes from `scripting` to `world` only, and `physics` knows nothing of animation.

## Decision

**A `LegIk` pass in `AnimationSystem`, in `PreRender` after the transform system and before the skin palette, rewrites the world matrices of the pelvis subtree with an analytic two-bone solve per leg, a pelvis drop and a foot turned to the ground. Local `Transform`s keep the animated pose. The game's script finds the ground in `lateUpdate` and writes it into the component.** These are contracts for the build card #500. This ADR adds no code by itself.

### The component

`LegIk` and `IkLeg` are reflected components of `world`, registered in `Components.cpp` with the others; `legs` is registered with `registerVector`. `LegIk` goes on the entity that carries the `Animator`.

```cpp
struct IkLeg {
  std::string thigh, calf, foot;     // paths under the Animator's entity
  glm::vec3 groundPoint{0};          // world space, written by the game
  glm::vec3 groundNormal{0, 1, 0};
  bool grounded{false};              // the ground query hit
};

struct LegIk {
  float weight{0};                   // overall weight, 0 to 1
  std::string pelvis;                // path under the Animator's entity
  float maxPelvisDrop{0.3f};         // m
  float maxFootAngle{0.5236f};       // radians (30°)
  float plantHeight;                 // m, contact weight 1 within it
  float liftHeight;                  // m, contact weight 0 at it
  float smoothing{0.08f};            // s
  std::vector<IkLeg> legs;
};
```

Because the components are reflected, they are part of the scene format, which freezes at 1.0.0; this contract has to be settled before then. They are new components, so existing scenes load unchanged; whether the scene version moves follows [ADR-0029](0029-compact-scene-files.md)'s rules for an additive change.

### Where the pass runs (amends ADR-0023 and ADR-0038)

1. **A `LegIk` system in `AnimationSystem`, in `PreRender`.** It is declared just before `SkinPalette`, so it runs after the transform system: flecs runs a phase's systems in declaration order, and `World::registerSystems` declares the transform system first.
   - The joints' `WorldTransform`s are this frame's, after `AnimationPlayback` (`Update`), `PhysicsInterpolate` and `ScriptLateUpdate` (`PostUpdate`), so the targets that scripts write in `lateUpdate` are already in place.
   - No application needs reordering. `AnimationSystem` is built before scripting in both `Game` and `Editor`, so a `PostUpdate` system there would run *before* `lateUpdate`; `PreRender` avoids that.
   - ADR-0038's step 5 becomes: the previous world transforms, the transform system, **leg IK**, the skin palettes, audio. ADR-0023's pose pipeline gains this post-pass between the transform propagation and the palette.
2. **The pass writes `WorldTransform` only.** Local `Transform`s keep the animated pose. It rewrites the world matrices of the pelvis subtree (the pelvis down, with any children such as `ball_l`) before the palette is built. So:
   - nothing builds up across frames: no pelvis creep while paused, or for a joint that no layer animates ("keeps the pose it last had", ADR-0038);
   - the inspector and the play-mode snapshot see the clip's pose;
   - Lua's `worldPosition()` reads the animated, pre-IK foot, which is where the ground query should start. `docs/scripting.md` says so.
3. **TAA needs nothing more.** Skinned motion comes from the renderer's per-instance previous skinned buffer, which last frame's skinning wrote, and `PreviousTransformSystem` copies last frame's IK'd world matrices. A pass before `SkinPalette` is therefore enough.

### Bones and the solve

4. **Bones are named by path under the `Animator`'s entity**, resolved and cached the way clip and skin bindings are. Each leg names its thigh, calf and foot; the component names the pelvis. A leg with a path that does not resolve is skipped, with one warning.
5. **The knee's pole is the animated knee.** The solve keeps the plane through the hip, the animated knee and the target. When the leg is straight (the three collinear), it falls back to the model's forward. There is no authored pole: the clips already bend the knee the right way, and nothing needs authoring.
6. **The solver** is a pure function, `solveTwoBone(hip, knee, ankle, target, pole, weight)`, testable in `world_tests` with hand-written targets.
   - It is an analytic two-bone solve by the law of cosines.
   - A target past the leg's length is clamped to `0.999 × (thigh + calf)`, so the result stays finite and the knee never flips.
   - Weight 0 returns the input pose bit-exactly. The weight blends the solved rotations with the animated ones by nlerp along the shorter arc, as the blend does.

### Targets, feet and pelvis

7. **Target = animated ankle + up × ground offset.** The offset is the ground point's height relative to the `Animator` entity's origin, which is the feet plane.
   - On flat ground the offset is 0 and the pass changes nothing, so the existing flat-ground clips, toe-speed tests and captures stay as they are.
   - The foot turns about the ankle by the shortest arc from up to the smoothed ground normal, clamped to `maxFootAngle` (default 30°, above the 20° ramp) and weighted.
8. **The pelvis** is lowered by the most negative weighted foot offset (the lower foot), clamped to `maxPelvisDrop` (default 0.3 m). It never rises; the raised foot bends its knee instead.
9. **Contact weight per foot comes from the animated pose, not new clip data.** It is 1 while the animated ankle is within `plantHeight` of its height in the skin's bind pose, and fades to 0 at `liftHeight`.
   - IK is therefore off during swing and fades in at contact, for every clip: turns, landings and idle included.
   - No format changes. It is the height test the retargeting used to define contacts (2.5 cm).

### Smoothing, pause and reset

10. **Smoothing is the engine's and frame-rate independent.** Each foot's offset and normal, the pelvis drop and the overall weight ease exponentially over `smoothing` seconds (default 0.08) by the frame's game delta.
    - **Time scale 0** gives a zero delta, which holds the smoothed values where they are.
    - **State.** It lives in `AnimationSystem` per entity, like `PlayState`.
    - **Reset.** The state resets, taking the new values at once, when the entity is first seen and when the model moves more than 2.5 m in one frame (the sample's teleport rule).
11. **Airborne.** The script sets the component's `weight` towards 0 while `physics.character(...).grounded` is false, and the smoothing fades it.

### The ground query (confirmed 2026-11-08)

- **The game's script runs it.** In `lateUpdate`, the sample's `character.lua` (or a new `feet.lua` on the model) runs one `physics.raycast` per foot.
  - Each ray starts at the animated ankle's xz and runs from `stepHeight` above the feet down to `stepHeight + maxPelvisDrop`, and ignores the character.
  - The script writes the hit's point, normal and whether it hit into each leg (`groundPoint`, `groundNormal`, `grounded`).
  - Data goes from `scripting` to `world` only (ADR-0022, ADR-0023), and the filter stays the game's. `physics` gains no animation logic.
- **Unreachable ground** (for example the sample's 0.5 m steps) separates the solver from the contact policy.
  - The pure solver clamps an unreachable target, as in 6, and stays finite with no knee flip.
  - The contact layer fades the IK weight to zero for unreachable ground, keeping the animated pose instead of claiming contact with that surface.

## Consequences

- A character's feet meet steps and slopes with one component on its model and a few lines of script, and nothing changes on flat ground.
- The solve is a pure function and the pass a world-matrix rewrite, so `world_tests` covers reach, clamping, weight 0, the pole side, flat ground, the pelvis cap, the foot angle, smoothing at different frame rates and at dt 0, and untouched local `Transform`s on a generated rig, without physics or scripts.
- What the inspector, the snapshot and `worldPosition()` show is the clip's pose, not the drawn one; the docs say so.
- A game that wants different ground (water, ignored layers) changes its script, not the engine.
- #500 builds the components, the pass, the sample's query and the capture scenes (`capture_step`, `capture_ramp`), and updates `docs/world.md`, `docs/scripting.md` and the sample README.
- Generic IK chains (hands, look-at), foot locking against sliding, new clip curve data and IK in edit mode with authored targets stay out of scope.

## Alternatives considered

- **Writing local `Transform`s**: the palette and every reader would see the IK'd pose, but the correction would build up across frames (pelvis creep while paused, unanimated joints keeping it), the inspector and snapshot would save it, and `worldPosition()` would no longer give the animated foot the query starts from.
- **A `PostUpdate` system in `AnimationSystem`**: it would run before `lateUpdate`, since `AnimationSystem` is built before scripting in both applications, so this frame's ground would arrive a frame late; reordering the applications is the cost of fixing it there.
- **An authored knee pole**: precise, but one more thing to author per rig, when the animated knee already bends the right way.
- **Contacts from clip curves or events**: exact per clip, but new clip data and a format change, while the bind-pose height test needs neither and covers every clip.
- **The ground query in `world` or `physics`**: no script needed, but `world` would call into `physics` or `physics` would learn about animation, against ADR-0022 and ADR-0023, and the filter would stop being the game's.
- **Claiming contact with unreachable ground**: the clamp alone keeps the solve finite, but the foot would claim contact with a surface it cannot reach; fading the weight keeps the animated pose there.
