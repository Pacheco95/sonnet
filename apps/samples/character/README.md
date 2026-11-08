# Character sample

A third-person character on an obstacle course, in the spirit of Unreal's Third Person template: a motion-captured humanoid that walks, runs, jumps and lands without its feet sliding, a follow camera that never goes through a wall, footsteps that sound like what they land on, and a course of steps, ramps, a narrow passage, a low tunnel, a moving platform over a pit and a ledge to jump onto. It plays in the editor's play mode, in the player from this folder and from a cooked bundle.

```bash
./build/linux-debug/apps/editor/sonnet_editor apps/samples/character      # then Play (Ctrl+P)
./build/linux-debug/apps/player/sonnet_player apps/samples/character
./build/linux-debug/apps/player/sonnet_player apps/samples/character --scene scenes/route.scene.json \
  --play 20 --screenshot route.png                                         # the route runs the course by itself
./build/linux-debug/apps/cook/sonnet_cook apps/samples/character --out build/character
./build/linux-debug/apps/player/sonnet_player build/character/game.sbundle   # the same, cooked
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

The camera asks for the pointer to be locked to the game when play starts (`input.setMouseLocked(true)`), and the player and the editor's play mode grant it while their window has the focus ([docs/player.md](../../../docs/player.md#the-pointer), [docs/editor.md](../../../docs/editor.md#the-pointer-in-play-mode)): Escape lets the pointer go and a click takes it back. While it is not locked, `input.mouseLocked()` is false and dragging looks around instead; the controls overlay says which is in effect.

## What is in it

| Path | |
|---|---|
| `scenes/main.scene.json` | The start scene: the course, its sky and sun, the character at the start, the camera and the controls overlay |
| `scenes/route.scene.json` | The same, with a `Route` the character follows instead of the keys, for captures and tests |
| `scenes/capture_*.scene.json` | Six stills of the character, below ([Captures](#captures)) |
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

The model turns towards the intended velocity once per rendered frame, smoothly and no faster than `facingRate`, and keeps its heading when the character stops. From standing, an input more than `turnThreshold` off that heading turns the character on the spot first ([Animation](#animation)). A move of more than 2.5 m in one step is a teleport (the kill volume's respawn, or a `Transform` written in the editor): the script starts over from there.

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
| `turnThreshold` | 60° | From standing, an input further off than this turns on the spot; 180 never does |
| `turnSpeed` | 1.25 | The turn clips' playback rate |
| `turnHold` | 0.6 s | The longest a turn on the spot holds the move back |
| `camera` | the scene's `Camera` | Whose yaw turns the keys |
| `route`, `routeTolerance` | none, 0.4 m | Below |

## Animation

The weights follow what the character does, not the keys. The ground speed is the horizontal length of the controller's velocity less its support's, so a character standing on the moving platform idles, and one pushing into a wall slows its feet.

- **Locomotion.** The `Animator` plays `Walk` as its clip and `Run` as a layer synchronised to it ([docs/world.md](../../../docs/world.md#animation)); both put the left foot down at the start of their cycle and the right at half. Below 0.9 m/s the walk blends into `Idle` by weight; between the clips' own speeds, 1.29 and 4.85 m/s, the walk blends into the run. The playback rate is the ground speed over the blended stride, in cycles per second, times the walk's length: a foot on the ground stays where it was put. It is kept within 0.7 and 1.4 of the blend's own rate, below which the idle has taken over.
- **Jumps and falls.** A jump starts `JumpStart` at 0.4 s, just before its take-off, and gives way to the looping `Fall` near its end or on the way down. Walking off an edge plays `Fall` once the character has been off the ground for 0.15 s or comes down faster than 2.5 m/s, so a step down plays nothing. The locomotion fades out over 0.12 s as the air takes over.
- **Turns on the spot** (issue #528). Standing still on the ground, an input more than `turnThreshold` (60°) off the model's heading plays a turn clip instead of swinging the idle round on its feet: `TurnLeft90` or `TurnRight90` below 135°, `TurnLeft180` or `TurnRight180` from there, to the side the input is on, at `turnSpeed`. The clips have their turn taken out at a constant rate ([LICENSES.md](LICENSES.md#turn-clips-issue-528)), so the model turns by the clip's measured angle (92.4°, -90.1°, 180° and -176.4°) times how much of it has played, with no other turning on top, and the feet stay where the clip puts them; the capsule keeps its yaw and root motion stays off. A turn fades in over 0.15 s and back into the idle over the last 0.3 s of its clip, and the little it falls short of the input is then turned smoothly. It holds the move back until the model faces within 45° of the input, for at most `turnHold`; a tap turns the character on the spot and nothing more. Once the character moves off, or jumps, the clip fades into the walk or run over 0.15 s and the usual facing finishes the turn. A smaller error keeps the usual facing, with no clip.
- **Landings.** Coming down faster than 2.5 m/s plays `Land` at a weight that grows with the speed, and faster than `hardLanding` `HardLand`. A landing fades out over the second half of its clip, sooner when the character runs on, and at once for another jump; one that is interrupted by a fall hands over to it. Each clip is started once, on the step its state begins; nothing is reset every frame.

## Footsteps

`footsteps.lua` is on the model, which carries the `Animator`, so it hears the clips' `footstep` and `landing` events. A step sounds only while the controller stands on something, from the surface it stands on: the support's material, or its parent's for the ramps' colliders, decides concrete, metal or wood. Walk and Run cross the same event in the same frame when they are blended, and a landing clip's own step can follow a walking step closely, so a foot sounds at most once every 0.3 s (a foot steps every 0.48 s at the fastest playback). Each step plays another recording of that foot and surface than the last one, from the foot's own source so a step never cuts the previous one short, with its pitch within 5% of the recording's and its volume up to 15% lower, louder at a run than at a walk.

## The camera

`camera.lua` is on the scene's `Camera` and places it in `lateUpdate`, after physics has interpolated the character to where the frame draws it. The mouse's motion is a displacement in pixels, turned into degrees by `sensitivity` and never scaled by the frame's time. The view looks at a pivot `height` (1.55 m) above the feet, `distance` (4 m) away; the pivot follows the feet with exponential smoothing that does not depend on the frame rate (`follow`, `rise`), and does nothing in a frame with no time, at a time scale of 0.

Two sphere sweeps keep the camera clear of the course, ignoring the character, and triggers are never hit: one from the character's chest to the pivot, so a pivot pressed into the low ceiling or a wall starts from inside the capsule instead, and one from there back to where the camera should be. The sphere holds the corners of the near plane at an aspect of up to 2, plus 5 cm, so the view is never cut by a wall. A hit pulls the camera in at once; when the way clears, it eases back out at `ease`, never past where the sweep stopped. A respawn moves the pivot at once. Pressed against a wall, the camera would be inside the character's head, so the character fades out as the camera closes in: fully drawn down to `fadeStart` (1 m) from the pivot, gone at `fadeEnd` (0.6 m), and in between at an alpha that falls linearly with the distance. `fadeEnd` is kept at least the near plane's sphere plus the capsule's radius from the pivot, so the near plane never cuts into a character that is still drawn. The fade is the alpha of every `MeshRenderer` under the character, which dithers an opaque material out pixel by pixel ([docs/rendering.md](../../../docs/rendering.md#resources-and-materials)): no blending, so temporal anti-aliasing and the depth stay intact, and the character keeps casting its whole shadow. The hair, eyes, eyebrows and eyelashes are masked materials, whose cutoff test multiplies the alpha in rather than dithering: they hold while the body fades, losing their soft edges first, and drop out once the alpha is below their cutoffs (0.35 for the hair, brows and lashes, 0.5 for the eyes). `runtime_tests` pulls the camera in with a wall moved towards the pivot in 0.1 m steps and checks that the fade follows the distance and never rises on the way in, and that it comes back to 1, with every colour its own again, as the camera eases out once the wall is gone.

`yaw` and `pitch` are where it starts, so a capture run, which repeats exactly, gets the same pose every time. Two properties stand in for the mouse in captures: `orbit` turns the view at a constant rate, and `trail` swings it behind the character's motion over the ground, which the route scene uses.

## The route

With the character's `route` property set to an entity, its children, in sibling order, steer the character instead of the keys, through the same intent, acceleration, jump buffer and animation. Each waypoint is reached within `routeTolerance` of it on the ground plane; the words of its name are cues:

| Word | Cue |
|---|---|
| `walk` | Walk the leg to this waypoint |
| `jump` | Press jump on reaching it |
| `wait <seconds>` | Stand there that long |
| `ride` | Stand there until the ground under the next waypoint has been away or moving and is now still: the platform, come to rest |
| `turn` | Turn on the spot to face the next waypoint, if that is a turn, and set off only once it has played |

The character slows into a waypoint where it will stand or where the route turns by more than 60°, and logs `Character finished its route` after the last one. A respawn starts the route over, and so does every play, since stopping play drops every script's state. `scenes/route.scene.json` runs the whole course this way: through the passage and the tunnel, up the ramp, across the deck and down its steps, along the pit's edge under the beam, over the pit on the platform, onto the ledge block with a jump and off its far side.

## Captures

Each `capture_*` scene is the course with the character started somewhere else, a short route made of the route scene's own legs, and the camera at a fixed pose instead of trailing, with no controls overlay. `--play` is in seconds of fixed 1/60 s steps, so a capture is the same frame every run; these land on the moment each scene is for:

| Scene | `--play` | What it shows |
|---|---|---|
| `capture_idle` | 3 | Standing at the start, from the front and a little to the right: the skin, the idle pose and the feet on the floor in the sun |
| `capture_locomotion` | 2 | Running east across the open floor at 4.8 m/s, from the south: the run's stride, a planted foot and the course behind |
| `capture_jump` | 1.8 | The route's jump onto the ledge block, from the east, at the top of the jump over the block's edge |
| `capture_landing` | 1.4 | Off the ledge block's far side, 1.2 m down, from the east: both feet down and the knees bent as `Land` plays |
| `capture_turn` | 0.6 | Standing at the start, from the front and a little to the right, a quarter turn to the left on the spot (`TurnLeft90`) halfway, the left foot stepping round |
| `capture_camera_obstruction` | 4 | Walking south into the low tunnel with the camera behind and above: the ceiling holds the camera under it, about 1.2 m from the pivot, and the character stays in view |

```bash
./build/linux-debug/apps/player/sonnet_player apps/samples/character --scene scenes/capture_jump.scene.json \
  --play 1.8 --screenshot jump.png
./build/linux-debug/apps/editor/sonnet_editor apps/samples/character --scene scenes/capture_jump.scene.json \
  --play 1.8 --front game --screenshot-window jump_window.png              # the Game panel, through the scene's camera
```

The editor's `--screenshot` is its Viewport, through the editor's own camera, so a still through the follow camera is the window with the Game panel in front. The player and the editor's Game panel show the same pose at the same `--play`. The scenes are written by `tools/generate_character_level.py level`, with the rest; `CAPTURES` there holds each one's start, route, camera and time.

## Tests

`runtime_tests` plays the course from this folder: its colliders lie where it draws them, the platform shuttles, and the character stands at the start and comes back there when it falls into the pit. From the keyboard, the character reaches the run speed and no more on a diagonal, stops, walks with Shift, faces the way it goes and jumps 1.27 m; a Space press just before landing jumps again and one long before is dropped. Walking off the ledge block, a jump 0.03 s after the feet leave it is taken and one 0.2 s after is not; a 0.25 m hop picks no landing clip, a 1 m drop the soft one at about three quarters of its weight and a 4.5 m drop the hard one; running off a hard landing starts at once and fades it faster than standing through it, and a jump pressed during it fades it out without starting its clip again. Tapping a key a quarter turn and then a half turn off the model's heading plays `TurnRight90` and then a half turn to the end, the model ends facing the key's direction within 1°, the capsule does not move, and each foot's toes, while planted (within 2.5 cm of their lowest and slower than 0.6 m/s), travel under 0.7 of the arc they would sweep if the idle were swung round on them (0.5 to 0.6 measured); an input 45° off plays no turn clip. The route scene runs the course to the end without a respawn or a warning; on the way the planted toes move at a median of 0.16 m/s while running at 4.8 m/s and 0.08 m/s while walking, no foot sounds twice for one step, and the camera's near-plane sphere never overlaps the course. The route starts over after a respawn and when the scene is opened again.

The same route plays from this folder and from the bundle the tests cook from it, and the two runs agree to the step: the frame the route ends on, where the character ends, the number of footsteps and the highest weight each clip reached (Run, JumpStart, Fall, Land and the quarter turn on the spot the route starts with; never HardLand). The bundle also runs `capture_landing` with the player's own flags. Rendered at 30, 60 and 144 frames a second, two seconds of running east, from standing already turned east, cover 8.52 m (8.60 m at 144, where a key can land up to a step before the step that reads it), a Space press in a frame without a step still jumps, 1.23 m high at every rate (1.27 m in closed form, less what steps of 1/60 s lose), and the camera moves by the same distance every frame, within 0.02 %, while the character runs at a steady speed.

The tests' copy of this folder has 1×1 textures and a 4×2 sky in place of the real ones, so the humanoid's images, the course's textures and the sky are not cooked on every run (the colours are nothing the tests check). The captures above are where the real ones are looked at.

## Not done here

Foot IK on slopes and steps ([#500](https://github.com/Pacheco95/sonnet/issues/500)): the feet are placed for flat ground. The turn clips come from another performer than the locomotion clips, so the idle's posture changes a little through a turn, and there is no crouch, sprint or gamepad input.
