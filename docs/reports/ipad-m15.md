# M15 sample and HUD budget on iPad

Measured 2026-10-12 at `1d03058b17f600519eecbc7e81a96606da99badd`, from `feat/hud-stress-scene` (main did not yet contain its width-scaling fix), on branch `mac-validation/m15-ipad-hud`. No engine code changed.

## Setup

- iPad, A16, iPadOS 27.0; `ios-release`, RelWithDebInfo, Xcode Apple Clang, MoltenVK. GPU core count and clock were not measured.
- Owner confirmed unlocked, awake, full screen, portrait, on a stand, Auto-Lock disabled, charging over USB-C to the Mac, case off and unused during measurements. Initial menu and playground captures were windowed and excluded; the owner selected Full Screen before all reported captures. Native sample PNGs are 1640×2360. Conditions were unchanged between reported runs.
- Cook for iOS: 47 assets, eight scenes and prefabs, zero warnings. The cooked index contains `scenes/hud_stress.scene.json`, alongside menu, playground and stress. Loaded and restored cooks have the same counts.
- Local, uncommitted build override `TARGETED_DEVICE_FAMILY=1,2` enabled installation on iPad. Generated `UIDeviceFamily` changed as follows:

```diff
 <key>UIDeviceFamily</key>
 <array>
     <integer>1</integer>
+    <integer>2</integer>
 </array>
```

The loaded case temporarily replaced `hud_stress.scene.json` with the stress scene plus the original HUD entity. The tracked scene was restored byte-for-byte, the normal bundle re-cooked, and the device-family build override removed afterward. Only this report and its two PNGs are committed.

## Part A

- PASS: full-screen menu capture ended `exit ok`; inspected by the agent: nine-sliced panel, labelled Play and Options, no Quit.
- PASS: playground capture ended `exit ok` and shows `Score 5` top left after three seconds; the score label renders. Inspected by the agent. The task asked for `Score 0`, which was its own mistake: a Linux capture with the same flags also logs `Physics crate picked up Crate coin: score 5` within those three seconds, so 5 is the expected score.
- PASS: playground capture shows Pause top right, inspected by the agent.

- PASS: Options opens the popup; performed by the owner.
- PASS: Close shuts the popup; performed by the owner.
- PASS: Play loads the playground with its HUD; performed by the owner.
- PASS: Pause stops the game and shows the pause menu; performed by the owner.
- PASS: Resume continues the game; performed by the owner.
- PASS: Pause stops the game again; performed by the owner.
- PASS: Main menu returns to the title; performed by the owner.

[Menu capture](ipad-m15/menu.png) and [playground capture](ipad-m15/playground.png), downscaled to 800 pixels tall.

## Part B

Every run is a fresh process using `--console --terminate-existing`, `--play 3 --render-size 1080x1920` and a screenshot. All logged `capture draws at 1080x1920, scaled to the window` and `exit ok`. Each capture emitted the same four MoltenVK startup warnings about blending on `VK_FORMAT_R32_UINT` seen in earlier reports; no engine error was logged. Inspected screenshots show all twenty columns and ten rows, filling the top third; the loaded screenshot also shows the boxes and spheres below the full grid. Times average the final hundred frames; CPU excludes GPU/display waits. Original results are retained, with one retry each for loaded runs 1 and 2 because overlay was unavailable; run 3 had numeric timings and needed no retry.

| Case / run | CPU ms | Interval ms | GPU ms | Overlay ms | Tonemap ms | Thermal, exactly logged |
|---|---:|---:|---:|---:|---:|---|
| idle-1 | 1.82 | 16.66 | 11.846 | 0.132 | 0.477 | `; thermal nominal -> nominal (worst nominal)` |
| idle-2 | 1.85 | 16.67 | 12.057 | 0.146 | 0.561 | `; thermal nominal -> nominal (worst nominal)` |
| idle-3 | 1.89 | 16.67 | 13.374 | 0.139 | 0.574 | `; thermal nominal -> nominal (worst nominal)` |
| loaded-1 | 8.25 | 27.83 | n/a | n/a | n/a | `; thermal nominal -> nominal (worst nominal)` |
| loaded-2 | 8.12 | 27.66 | n/a | n/a | n/a | `; thermal nominal -> nominal (worst nominal)` |
| loaded-3 | 8.27 | 27.51 | 27.035 | 0.060 | 0.167 | `; thermal nominal -> nominal (worst nominal)` |
| loaded-retry-1 | 8.25 | 27.50 | 26.936 | 0.077 | 0.154 | `; thermal nominal -> nominal (worst nominal)` |
| loaded-retry-2 | 8.41 | 27.81 | n/a | n/a | n/a | `; thermal nominal -> nominal (worst nominal)` |
| reference | 8.20 | 27.50 | 26.568 | absent | 0.201 | `; thermal nominal -> nominal (worst nominal)` |

The loaded overlay averages **0.069 ms** across two numeric runs (0.060 and 0.077 ms), both under 0.2 ms; three loaded runs including a retry have unavailable GPU timings, so the budget is supported only by the available samples. Idle averages **0.139 ms** across three runs, about twice the loaded mean and below the S25's 0.49 ms idle figure at its lowest clock; iOS exposes no GPU clock to the player, so these runs cannot attribute the idle-versus-loaded difference to clock changes. Numeric loaded GPU totals average 26.986 ms versus the single reference's 26.568 ms, while intervals remain about 27.5 ms; this is a short comparison with variable passes, not an isolated measurement of added HUD work. The display caps frames at 60 Hz (idle intervals are about 16.67 ms), and these captures do not independently validate GPU timestamps, establish sustained performance or apply to other devices.

Context: [previous iPad measurement](ipad-m14.md), [iPad pass measurements](ipad-stress-passes.md), and [HUD capture procedure](../player.md#capture-runs).

<details>
<summary>Final capture lines, in execution order</summary>

idle-1:

```text
capture frame times over the last 100 frames: CPU 1.82 ms a frame, 16.66 ms apart; GPU 11.846 ms: depth 0.147 ms, light clustering 0.351 ms, forward 1.110 ms, taa 8.331 ms, bloom down 0 0.152 ms, bloom down 1 0.097 ms, bloom down 2 0.051 ms, bloom down 3 0.052 ms, bloom down 4 0.044 ms, bloom up 3 0.044 ms, bloom up 2 0.039 ms, bloom up 1 0.038 ms, bloom up 0 0.073 ms, tonemap 0.477 ms, overlay 0.132 ms, present 0.709 ms; thermal nominal -> nominal (worst nominal)
```

idle-2:

```text
capture frame times over the last 100 frames: CPU 1.85 ms a frame, 16.67 ms apart; GPU 12.057 ms: depth 0.097 ms, light clustering 0.233 ms, forward 1.188 ms, taa 8.518 ms, bloom down 0 0.153 ms, bloom down 1 0.128 ms, bloom down 2 0.048 ms, bloom down 3 0.056 ms, bloom down 4 0.044 ms, bloom up 3 0.034 ms, bloom up 2 0.039 ms, bloom up 1 0.041 ms, bloom up 0 0.109 ms, tonemap 0.561 ms, overlay 0.146 ms, present 0.662 ms; thermal nominal -> nominal (worst nominal)
```

idle-3:

```text
capture frame times over the last 100 frames: CPU 1.89 ms a frame, 16.67 ms apart; GPU 13.374 ms: depth 0.138 ms, light clustering 0.281 ms, forward 1.394 ms, taa 9.486 ms, bloom down 0 0.089 ms, bloom down 1 0.145 ms, bloom down 2 0.055 ms, bloom down 3 0.059 ms, bloom down 4 0.044 ms, bloom up 3 0.040 ms, bloom up 2 0.040 ms, bloom up 1 0.041 ms, bloom up 0 0.112 ms, tonemap 0.574 ms, overlay 0.139 ms, present 0.736 ms; thermal nominal -> nominal (worst nominal)
```

loaded-1:

```text
capture frame times over the last 100 frames: CPU 8.25 ms a frame, 27.83 ms apart; GPU n/a: cull n/a, shadow cascade 0 n/a, shadow cascade 1 n/a, shadow cascade 2 n/a, shadow cascade 3 n/a, depth n/a, depth pyramid n/a, occlusion cull n/a, depth late n/a, light clustering n/a, forward n/a, taa n/a, bloom down 0 n/a, bloom down 1 n/a, bloom down 2 n/a, bloom down 3 n/a, bloom down 4 n/a, bloom up 3 n/a, bloom up 2 n/a, bloom up 1 n/a, bloom up 0 n/a, tonemap n/a, overlay n/a, present n/a; thermal nominal -> nominal (worst nominal)
```

loaded-2:

```text
capture frame times over the last 100 frames: CPU 8.12 ms a frame, 27.66 ms apart; GPU n/a: cull n/a, shadow cascade 0 n/a, shadow cascade 1 n/a, shadow cascade 2 n/a, shadow cascade 3 n/a, depth n/a, depth pyramid n/a, occlusion cull n/a, depth late n/a, light clustering n/a, forward n/a, taa n/a, bloom down 0 n/a, bloom down 1 n/a, bloom down 2 n/a, bloom down 3 n/a, bloom down 4 n/a, bloom up 3 n/a, bloom up 2 n/a, bloom up 1 n/a, bloom up 0 n/a, tonemap n/a, overlay n/a, present n/a; thermal nominal -> nominal (worst nominal)
```

loaded-3:

```text
capture frame times over the last 100 frames: CPU 8.27 ms a frame, 27.51 ms apart; GPU 27.035 ms: cull 0.119 ms, shadow cascade 0 0.131 ms, shadow cascade 1 0.410 ms, shadow cascade 2 0.892 ms, shadow cascade 3 1.191 ms, depth 1.587 ms, depth pyramid 0.910 ms, occlusion cull 0.044 ms, depth late 0.023 ms, light clustering 0.075 ms, forward 17.813 ms, taa 2.568 ms, bloom down 0 0.269 ms, bloom down 1 0.037 ms, bloom down 2 0.041 ms, bloom down 3 0.024 ms, bloom down 4 0.023 ms, bloom up 3 0.015 ms, bloom up 2 0.019 ms, bloom up 1 0.017 ms, bloom up 0 0.018 ms, tonemap 0.167 ms, overlay 0.060 ms, present 0.584 ms; thermal nominal -> nominal (worst nominal)
```

loaded-retry-1:

```text
capture frame times over the last 100 frames: CPU 8.25 ms a frame, 27.50 ms apart; GPU 26.936 ms: cull 0.124 ms, shadow cascade 0 0.132 ms, shadow cascade 1 0.473 ms, shadow cascade 2 0.820 ms, shadow cascade 3 1.194 ms, depth 1.664 ms, depth pyramid 0.836 ms, occlusion cull 0.031 ms, depth late 0.022 ms, light clustering 0.048 ms, forward 17.829 ms, taa 2.772 ms, bloom down 0 0.013 ms, bloom down 1 0.035 ms, bloom down 2 0.048 ms, bloom down 3 0.031 ms, bloom down 4 0.024 ms, bloom up 3 0.024 ms, bloom up 2 0.038 ms, bloom up 1 0.020 ms, bloom up 0 0.022 ms, tonemap 0.154 ms, overlay 0.077 ms, present 0.505 ms; thermal nominal -> nominal (worst nominal)
```

loaded-retry-2:

```text
capture frame times over the last 100 frames: CPU 8.41 ms a frame, 27.81 ms apart; GPU n/a: cull n/a, shadow cascade 0 n/a, shadow cascade 1 n/a, shadow cascade 2 n/a, shadow cascade 3 n/a, depth n/a, depth pyramid n/a, occlusion cull n/a, depth late n/a, light clustering n/a, forward n/a, taa n/a, bloom down 0 n/a, bloom down 1 n/a, bloom down 2 n/a, bloom down 3 n/a, bloom down 4 n/a, bloom up 3 n/a, bloom up 2 n/a, bloom up 1 n/a, bloom up 0 n/a, tonemap n/a, overlay n/a, present n/a; thermal nominal -> nominal (worst nominal)
```

reference:

```text
capture frame times over the last 100 frames: CPU 8.20 ms a frame, 27.50 ms apart; GPU 26.568 ms: cull 0.124 ms, shadow cascade 0 0.066 ms, shadow cascade 1 0.051 ms, shadow cascade 2 0.964 ms, shadow cascade 3 1.155 ms, depth 1.769 ms, depth pyramid 0.841 ms, occlusion cull 0.036 ms, depth late 0.023 ms, light clustering 0.056 ms, forward 17.713 ms, taa 2.602 ms, bloom down 0 0.278 ms, bloom down 1 0.051 ms, bloom down 2 0.040 ms, bloom down 3 0.029 ms, bloom down 4 0.024 ms, bloom up 3 0.022 ms, bloom up 2 0.020 ms, bloom up 1 0.016 ms, bloom up 0 0.015 ms, tonemap 0.201 ms, present 0.472 ms; thermal nominal -> nominal (worst nominal)
```

</details>
