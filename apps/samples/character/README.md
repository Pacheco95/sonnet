# Character sample

A third-person character on an obstacle course, in the spirit of Unreal's Third Person template: a motion-captured humanoid that walks, runs, jumps and lands without its feet sliding, a follow camera that never goes through a wall, footsteps that sound like what they land on, and a course of steps, ramps, a narrow passage, a low tunnel, a moving platform over a pit and a ledge to jump onto. It plays in the editor's play mode, in the player from this folder and from a cooked bundle.

```bash
./build/linux-debug/apps/editor/sonnet_editor apps/samples/character      # then Play (Ctrl+P)
./build/linux-debug/apps/player/sonnet_player apps/samples/character
./build/linux-debug/apps/player/sonnet_player apps/samples/character --scene scenes/route.scene.json \
  --play 20 --screenshot route.png                                         # the route runs the course by itself
```

## Controls

| Input | |
|---|---|
| W A S D or the arrow keys | Move, relative to where the camera looks; a diagonal is as fast as a straight line |
| Shift (held) | Walk instead of running |
| Space | Jump; a press up to 0.15 s before landing, or up to 0.12 s after walking off an edge, still jumps |
| Mouse | Look around: the mouse's motion while the pointer is locked to the game, otherwise dragging with either button |
| Wheel | Zoom between 1.5 and 7 m |
| H | Hide or show the controls |

The camera asks for the pointer to be locked to the game when play starts (`input.setMouseLocked(true)`). Until the application grants that ([#495](https://github.com/Pacheco95/sonnet/issues/495)), `input.mouseLocked()` is false and dragging looks around instead; the controls overlay says which is in effect.

## What is in it

| Path | |
|---|---|
| `scenes/main.scene.json` | The start scene: the course, its sky and sun, the character at the start, the camera and the controls overlay |
| `scenes/route.scene.json` | The same, with a `Route` the character follows instead of the keys, for captures and tests |
| `prefabs/character.prefab.json` | The character: the root with the `CharacterController` and `character.lua`, the humanoid as its `Model` child with `footsteps.lua`, and three sound sources (`Left foot`, `Right foot`, `Landing`) |
| `scripts/character.lua` | Movement, jumping, facing and the animation weights |
| `scripts/camera.lua` | The follow camera, on the scene's `Camera` |
| `scripts/footsteps.lua` | Footstep and landing sounds from the clips' events |
| `scripts/controls.lua` | The controls overlay, on its canvas |
| `scripts/platform.lua`, `scripts/kill_volume.lua` | The platform over the pit and the trigger under the course that puts what falls back at `Respawn` |
| `assets/character/humanoid.glb` | The humanoid and its clips ([LICENSES.md](LICENSES.md)) |
| `assets/level/`, `assets/audio/footsteps/` | The course's geometry, materials and sky; the foley |
| `assets/fonts/` | Noto Sans for the overlay ([its README](assets/fonts/README.md)) |

`tools/generate_character_level.py level` writes the scenes, the prefab, the course's geometry and the materials, so a change to any of them goes there and the files are written again; the scripts are written by hand.

## The character

The root entity carries the capsule, a `CharacterController` 1.8 m tall and 0.3 m in radius whose origin is the feet ([docs/physics.md](../../../docs/physics.md#characters)), and `character.lua`. It never turns: the `Model` child is what faces where the character goes, and the camera is a separate entity, outside the character, so neither the controller nor the camera's sweeps ever meet the model. Root motion is off; the controller moves the character and the clips are played in place.

Each rendered frame, `update` notes a Space press, which lasts one frame and may come in a frame with no physics step, and keeps it for `jumpBuffer` seconds. Each fixed step, `fixedUpdate`:

1. Reads the keys, which are state, as a direction no longer than 1, turned by the camera's yaw.
2. Accelerates the intended horizontal velocity towards that direction times `runSpeed` (or `walkSpeed` with Shift) by `acceleration`, or by `braking` when slowing down or turning back, scaled by `airControl` in the air. On the ground the intent stays within 1.5 m/s of what the controller achieves, so walking into a wall stores no speed. `physics.setCharacterMove` hands it over.
3. Jumps with `physics.jumpCharacter` when a buffered press meets the ground, or the coyote time after leaving it, once per time on the ground.
4. Sets the animation weights from `physics.character`, what the step just did, so the same frame's playback uses them (ADR-0038).

The model turns towards the intended velocity once per rendered frame, smoothly and no faster than `facingRate`, and keeps its heading when the character stops. A move of more than 2.5 m in one step is a teleport (the kill volume's respawn, or a `Transform` written in the editor): the script starts over from there.

| Property | Default | |
|---|---|---|
| `runSpeed`, `walkSpeed` | 4.8, 1.5 m/s | Close to the clips' own 4.85 and 1.29 m/s |
| `acceleration`, `braking` | 12, 16 m/s² | |
| `airControl` | 0.3 | Of the acceleration and braking, in the air |
| `facingRate` | 540 °/s | |
| `jumpSpeed` | 5 m/s | 1.27 m high: enough for the 1.2 m ledge block |
| `jumpBuffer`, `coyoteTime` | 0.15, 0.12 s | |
| `walkStride`, `runStride` | 1.505, 3.235 m | The clips' measured strides ([LICENSES.md](LICENSES.md#locomotion-clips-issue-505)) |
| `hardLanding` | 7 m/s | Coming down faster than this plays `HardLand` |
| `camera` | the scene's `Camera` | Whose yaw turns the keys |
| `route`, `routeTolerance` | none, 0.4 m | Below |

## Animation

The weights follow what the character does, not the keys. The ground speed is the horizontal length of the controller's velocity less its support's, so a character standing on the moving platform idles, and one pushing into a wall slows its feet.

- **Locomotion.** The `Animator` plays `Walk` as its clip and `Run` as a layer synchronised to it ([docs/world.md](../../../docs/world.md#animation)); both put the left foot down at the start of their cycle and the right at half. Below 0.9 m/s the walk blends into `Idle` by weight; between the clips' own speeds, 1.29 and 4.85 m/s, the walk blends into the run. The playback rate is the ground speed over the blended stride, in cycles per second, times the walk's length: a foot on the ground stays where it was put. It is kept within 0.7 and 1.4 of the blend's own rate, below which the idle has taken over.
- **Jumps and falls.** A jump starts `JumpStart` at 0.4 s, just before its take-off, and gives way to the looping `Fall` near its end or on the way down. Walking off an edge plays `Fall` once the character has been off the ground for 0.15 s or comes down faster than 2.5 m/s, so a step down plays nothing. The locomotion fades out over 0.12 s as the air takes over.
- **Landings.** Coming down faster than 2.5 m/s plays `Land` at a weight that grows with the speed, and faster than `hardLanding` `HardLand`. A landing fades out over the second half of its clip, sooner when the character runs on, and at once for another jump; one that is interrupted by a fall hands over to it. Each clip is started once, on the step its state begins; nothing is reset every frame.

## Footsteps

`footsteps.lua` is on the model, which carries the `Animator`, so it hears the clips' `footstep` and `landing` events. A step sounds only while the controller stands on something, from the surface it stands on: the support's material, or its parent's for the ramps' colliders, decides concrete, metal or wood. Walk and Run cross the same event in the same frame when they are blended, and a landing clip's own step can follow a walking step closely, so a foot sounds at most once every 0.3 s (a foot steps every 0.48 s at the fastest playback). Each step plays another recording of that foot and surface than the last one, from the foot's own source so a step never cuts the previous one short, with its pitch within 5% of the recording's and its volume up to 15% lower, louder at a run than at a walk.

## The camera

`camera.lua` is on the scene's `Camera` and places it in `lateUpdate`, after physics has interpolated the character to where the frame draws it. The mouse's motion is a displacement in pixels, turned into degrees by `sensitivity` and never scaled by the frame's time. The view looks at a pivot `height` (1.55 m) above the feet, `distance` (4 m) away; the pivot follows the feet with exponential smoothing that does not depend on the frame rate (`follow`, `rise`), and does nothing in a frame with no time, at a time scale of 0.

Two sphere sweeps keep the camera clear of the course, ignoring the character, and triggers are never hit: one from the character's chest to the pivot, so a pivot pressed into the low ceiling or a wall starts from inside the capsule instead, and one from there back to where the camera should be. The sphere holds the corners of the near plane at an aspect of up to 2, plus 5 cm, so the view is never cut by a wall. A hit pulls the camera in at once; when the way clears, it eases back out at `ease`, never past where the sweep stopped. A respawn moves the pivot at once. Pressed against a wall, the camera would be inside the character's head, so the character is hidden while the camera is closer than `hideWithin` (0.6 m) to the pivot.

`yaw` and `pitch` are where it starts, so a capture run, which repeats exactly, gets the same pose every time. Two properties stand in for the mouse in captures: `orbit` turns the view at a constant rate, and `trail` swings it behind the character's motion over the ground, which the route scene uses.

## The route

With the character's `route` property set to an entity, its children, in sibling order, steer the character instead of the keys, through the same intent, acceleration, jump buffer and animation. Each waypoint is reached within `routeTolerance` of it on the ground plane; the words of its name are cues:

| Word | Cue |
|---|---|
| `walk` | Walk the leg to this waypoint |
| `jump` | Press jump on reaching it |
| `wait <seconds>` | Stand there that long |
| `ride` | Stand there until the ground under the next waypoint has been away or moving and is now still: the platform, come to rest |

The character slows into a waypoint where it will stand or where the route turns by more than 60°, and logs `Character finished its route` after the last one. A respawn starts the route over, and so does every play, since stopping play drops every script's state. `scenes/route.scene.json` runs the whole course this way: through the passage and the tunnel, up the ramp, across the deck and down its steps, along the pit's edge under the beam, over the pit on the platform, onto the ledge block with a jump and off its far side.

## Tests

`runtime_tests` plays the course from this folder: its colliders lie where it draws them, the platform shuttles, and the character stands at the start and comes back there when it falls into the pit. From the keyboard, the character reaches the run speed and no more on a diagonal, stops, walks with Shift, faces the way it goes and jumps 1.27 m; a Space press just before landing jumps again and one long before is dropped. The route scene runs the course to the end without a respawn or a warning; on the way the planted toes move at a median of 0.18 m/s while running at 4.8 m/s and 0.08 m/s while walking, no foot sounds twice for one step, and the camera's near-plane sphere never overlaps the course. The route starts over after a respawn and when the scene is opened again.

## Not done here

Foot IK on slopes and steps ([#500](https://github.com/Pacheco95/sonnet/issues/500)): the feet are placed for flat ground. Turning on the spot plays no turn clip, and there is no crouch, sprint or gamepad input.
