# The themed editor at 100 to 200 % on Windows

The Windows part of [#392](https://github.com/Pacheco95/sonnet/issues/392), run for [#418](https://github.com/Pacheco95/sonnet/issues/418) on 2026-11-02.

## Setup

- Commit `b27beaf` (`origin/docs/editor-theme-scales`, which carries the display scale fix of [#421](https://github.com/Pacheco95/sonnet/pull/421); `main` did not have it yet), built with the `windows-release` preset, generic (no `SONNET_GAME`).
- Windows 11 Pro 10.0.26200.
- GPU: NVIDIA GeForce RTX 4090, driver 32.0.16.1088 (610.88). The machine also lists an AMD Radeon integrated GPU and a virtual display adapter, neither driving a monitor.
- One monitor: 2560x1440, usable area 2560x1392 at 100 % (the taskbar grows with the scale). The Scale list offers 100, 125, 150, 175, 200 and 225 %.

## Captures

Every run exited with code 0. The only `[warn]` lines were two Vulkan loader messages, at every scale, about a missing implicit-layer manifest of a third-party overlay (`EOSOverlayVkLayer-Win32.json` and `-Win64.json`); no `[error]` line. The debug lines print in the release build. The viewport panel was measured in each `basic` PNG as the scene area under the `main.scene.json` tab.

| Scale | Image size | Logged | Viewport target vs panel | Checked | Wrong |
|---|---|---|---|---|---|
| 100 % | 1600x900 | `display scale 1 -> 1` | 968x630 vs about 968x630 | close buttons on every tab and panel, 1 px outlines on fields and buttons, text size, List and Grid icons centred, statistics overlay clear of "User Perspective", dialog centred, Assets tiles, Log text | nothing; matches `docs/reports/editor-theme/basic-after.png` |
| 125 % | 2000x1125 | `display scale 1 -> 1.25` | 1189x779 vs about 1189x779 | the same | nothing |
| 150 % | 2400x1350 | `display scale 1 -> 1.5` | 1452x945 vs about 1452x944 | the same | nothing |
| 175 % | 2560x1356 (clamped to the usable area) | `display scale 1 -> 1.75` | 1433x876 vs about 1432x876 | the same, and the window filling the usable area | nothing |
| 200 % | 2560x1344 (clamped to the usable area) | `display scale 1 -> 2` | 1297x805 vs about 1297x805 | the same, and the window filling the usable area; tiles' thumbnails a little soft, as expected | nothing; proportions and theme match `../x11-basic-200.jpg` |

At 200 % the Inspector's color row and the Statistics table are cut by the panel below them, and the Log gains a horizontal scroll bar: the narrower panels explain both, and the Linux captures show the same. At every scale the render target matches the panel, so the half-resolution viewport of [#422](https://github.com/Pacheco95/sonnet/issues/422) does not occur on Windows (pixel density 1); `windows-viewport-200.png` is a 600x400 crop at 1:1 from the middle of the viewport at 200 % and is sharp.

## Live scale change

**Failed.** The editor, maximized on a scratch copy of `basic` at 100 %, followed 100 → 150 → 200 → 100 % on the next frame each time (`display scale 1 -> 1.5`, `1.5 -> 2`, `2 -> 1`), with text and controls resized, nothing cut and every close button and outline present. At 150 %, resizing by dragging a corner was smooth, with no jump or flicker; about 1,000 swapchain resizes logged no `display scale` line. After 200 → 100 %, the maximized window kept a 13 px frame (the 200 % size, where 100 % uses 8 px), at -13,-13 to 2563,1373: in the owner's screenshot the title bar and its buttons are cut by the top of the screen, the panels touch the left edge, and a strip above the taskbar is no longer drawn (the swapchain is 2560x1347, where a maximized window at 100 % gets about 2560x1369). The screenshot is not committed, since it shows the taskbar. The owner also saw the cursor stop short of the screen's top and left edges; that remained with the editor closed and is the monitor's pixel shift, not the editor (the system cursor clip was the whole screen).

## Panels across monitors

**Not checked:** the machine has one monitor.
