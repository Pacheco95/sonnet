# The themed editor at each display scale

[Issue 392](https://github.com/Pacheco95/sonnet/issues/392), the last check of [ADR-0036](../decisions/0036-dear-imgui-stays-the-editor-ui.md): the finished editor (the [Graphite](editor-theme.md) theme, the scale-derived style of #390, the first window and layouts of #410) at the scales it supports, on the platforms it ships on. Status: Linux checked on 2026-11-02 at `fea087e`, with the owner switching GNOME's scale for the Wayland runs; Windows and macOS are with device agents ([#418](https://github.com/Pacheco95/sonnet/issues/418), [#419](https://github.com/Pacheco95/sonnet/issues/419)) and **not checked** yet.

How the scale is chosen and what follows it is in [editor.md](../editor.md#display-scale). In short, the editor's scale is SDL's display scale divided by the window's pixel density: the desktop scale on X11 and Windows, 1 on a Retina or Wayland window, which the compositor scales.

## Method

Every capture is a `--screenshot-window` run ([Screenshots](../editor.md#screenshots)), which starts from the built-in layout and the default preferences, so captures differ only by the scale. Four per scale:

| Capture | Command (from the checkout) |
|---|---|
| `basic` | `sonnet_editor apps/samples/basic --select "Spinning box" --front log` |
| `basic-dialog` | `sonnet_editor apps/samples/basic --select "Spinning box" --dialog save-layout` |
| `basic-grid` | `sonnet_editor apps/samples/basic --select "Spinning box" --assets grid` |
| `showcase` | `sonnet_editor apps/samples/showcase --select Armillary --front log` |

The dialog is `save-layout` because the other dialogs show the project's absolute path. The Log and Assets panels share a tab bar, so the Log and the grid are separate captures. On X11 the scale comes from `SDL_VIDEO_X11_SCALING_FACTOR`; the pointer was parked outside the window, since on X11 a capture shows the hover under the real pointer. Every run exited 0 with no warning, error or validation message.

The captures are stored as JPEG (quality 90, no chroma subsampling) to keep the report about 8 MB; the 1:1 thumbnail crops are PNG.

Machine: Ubuntu 24.04, GNOME 46 on Wayland with XWayland, one 2560x1440 monitor at 100 %, RTX 4090, `linux-debug`.

## Linux, X11 (XWayland)

| Scale | Image | Result |
|---|---|---|
| 1 | 1600x900 | Passed. The [#391 look](editor-theme.md) as captured there. |
| 1.25 | 2000x1125 | Passed. Same layout as at 1 in logical units: text sharp at its baked size, icons centred in the Assets toolbar and the close buttons, the statistics overlay stops above "User Perspective", the dialog centred. |
| 1.5 | 2400x1350 | Passed, as at 1.25. |
| 2 | 2494x1371 | Passed. The first window (3200x1800 asked) is clamped to the display's usable area, so the window is about 1247x685 logical, smaller than at the other scales: the Inspector scrolls after the MeshRenderer's colour, the Hierarchy and the overlay show fewer rows, and the Log gets a horizontal scrollbar because its longest line is wider than the panel (lines do not wrap). Everything shown is laid out correctly; it is a smaller window, not a broken one ([#410](https://github.com/Pacheco95/sonnet/issues/410)). |

Scale 1:

![X11 at 1, basic](editor-theme-scales/x11-basic-100.jpg)

![X11 at 1, showcase](editor-theme-scales/x11-showcase-100.jpg)

Scale 1.25:

![X11 at 1.25, basic](editor-theme-scales/x11-basic-125.jpg)

![X11 at 1.25, save-layout dialog](editor-theme-scales/x11-basic-dialog-125.jpg)

![X11 at 1.25, Assets grid](editor-theme-scales/x11-basic-grid-125.jpg)

![X11 at 1.25, showcase](editor-theme-scales/x11-showcase-125.jpg)

Scale 1.5:

![X11 at 1.5, basic](editor-theme-scales/x11-basic-150.jpg)

![X11 at 1.5, save-layout dialog](editor-theme-scales/x11-basic-dialog-150.jpg)

![X11 at 1.5, Assets grid](editor-theme-scales/x11-basic-grid-150.jpg)

![X11 at 1.5, showcase](editor-theme-scales/x11-showcase-150.jpg)

Scale 2:

![X11 at 2, basic](editor-theme-scales/x11-basic-200.jpg)

![X11 at 2, save-layout dialog](editor-theme-scales/x11-basic-dialog-200.jpg)

![X11 at 2, Assets grid](editor-theme-scales/x11-basic-grid-200.jpg)

![X11 at 2, showcase](editor-theme-scales/x11-showcase-200.jpg)

The dialog and the grid at 1 are [the #391 captures](editor-theme.md#details-at-11), taken the same way: [dialog](editor-theme-scales/x11-basic-dialog-100.jpg), [grid](editor-theme-scales/x11-basic-grid-100.jpg).

### Thumbnails at 2

#390 accepted that a 128 px thumbnail is magnified above about scale 1.45. At 2 the default 96 px tile draws it at 192 px, 1.5 times its size: the pictures are a little soft and the silhouettes show slight steps, but every model reads clearly. **Accepted**; no change. The tile row at 1:1, at 1 and at 2:

![Thumbnails at 1](editor-theme-scales/x11-thumbnails-100.png)

![Thumbnails at 2](editor-theme-scales/x11-thumbnails-200.png)

### A floating panel at 1.5 and 2

#390 asked whether a floating panel stays inside the window and out from under the menu bar at a scale above 1. Checked live: the basic sample's `layout.ini`, saved by the editor at scale 1, with the Log panel's entry changed to float at 200,300 (640x360), then opened by an interactive editor at 1, 1.5 and 2 with a private `XDG_DATA_HOME`, and captured with `import -window`.

- X11 has multi-viewport on, so the editor keeps the saved position ([Layout presets](../editor.md#layout-presets)). At every scale the Log opened inside the window, below the menu bar, in the main window (no second OS window). **Passed.**
- The placement that moves floating panels inside the window below the menu bar (`placedForThisWindow`, #407) only runs with multi-viewport off, which is native Wayland, where the scale is always 1 (the window asks for high pixel density, so SDL's display scale and pixel density cancel). A scale above 1 with viewports off does not occur on any platform the editor ships on, so that path at those scales was not run.
- **Defect, filed as [#420](https://github.com/Pacheco95/sonnet/issues/420):** the layout keeps its pixel sizes at the new scale. Opened at 2, the Hierarchy keeps the 288 px it had at 1 (about 144 logical px), so names and its type combo are cut, the Statistics table's columns shrink to "cp…" and "gp…", and the floating Log is still 640x360 px, half its size in logical units. `editor.md` listed this as a limit; #420 asks for the layout to record its scale and be scaled on load.

![X11 at 1.5, a floating Log from a saved layout](editor-theme-scales/x11-floating-150.jpg)

![X11 at 2, the same layout](editor-theme-scales/x11-floating-200.jpg)

## Linux, native Wayland

`SDL_VIDEODRIVER=wayland`. The owner set GNOME's monitor scale (Settings > Displays) to 100 %, then 200 %, then 150 % with fractional scaling on. One editor stayed open across both switches (on a scratch copy of the basic sample, screenshots through the desktop portal), and the four captures ran as fresh processes at 200 % and 150 %. Wayland reports the monitor scale as pixel density, so the editor's scale should stay 1 and the layout should keep its logical size, drawn with more pixels. The only warning in every run is the window icon (`xdg_toplevel_icon_v1` is not supported by GNOME 46), which is unrelated.

| Monitor scale | Image | Result |
|---|---|---|
| 100 % | 1600x900 | Passed. Scale 1, matches X11 at 1. |
| 200 %, switched live | 2428x1376 | Passed for the UI. With no restart, the swapchain was rebuilt at twice the density, the scale stayed 1 and the editor redrew sharp with the same layout; GNOME shrank the window to fit the 1280x720 logical desktop. |
| 200 %, fresh captures | 2428x1376 | Passed for the UI: basic, dialog, grid and showcase laid out as at 1 in a window of about 1214x688 logical, text and icons sharp. **Defect:** the viewport's scene is rendered at half resolution and stretched (below). |
| 150 %, switched live | 2394x1347 | **Defect, fixed in this PR:** the editor's scale became 0.99986, not 1, and every close button and 1 px outline disappeared (below). |
| 150 %, fresh captures | 2455x1389 | Passed, by luck: the scale read 0.99986 and then 1.00012 as the window settled, and at 1.00012 nothing truncates. |

At 100 %:

![Wayland at 100 %](editor-theme-scales/wayland-basic-100.jpg)

At 200 %, the running editor after the switch, and the fresh captures:

![Wayland, switched live to 200 %](editor-theme-scales/wayland-live-200.jpg)

![Wayland at 200 %, basic](editor-theme-scales/wayland-basic-200.jpg)

![Wayland at 200 %, save-layout dialog](editor-theme-scales/wayland-basic-dialog-200.jpg)

![Wayland at 200 %, Assets grid](editor-theme-scales/wayland-basic-grid-200.jpg)

![Wayland at 200 %, showcase](editor-theme-scales/wayland-showcase-200.jpg)

At 150 %, a fresh capture:

![Wayland at 150 %, basic](editor-theme-scales/wayland-basic-150.jpg)

### The viewport at half resolution (filed, #422)

The viewport's render target is the panel's size in Dear ImGui's units, not in pixels: at 200 % the panel is about 1164x836 pixels and the log says `render target "viewport" 582x418`. The scene and the selection outline are blocky, while the gizmo and the UI, which Dear ImGui draws at the framebuffer's density, are sharp. On X11 at 2 (density 1) the target matches the panel and the view is sharp. Every Retina Mac is expected to show the same. Filed as [#422](https://github.com/Pacheco95/sonnet/issues/422). The viewport at 1:1, Wayland at 200 % and X11 at 2:

![Viewport, Wayland at 200 %](editor-theme-scales/wayland-viewport-200.png)

![Viewport, X11 at 2](editor-theme-scales/x11-viewport-200.png)

### Close buttons and outlines lost at 150 % (fixed)

A fractional Wayland window's pixel density comes from whole pixel sizes, so SDL's display scale over it lands near 1 instead of on it: 0.99986, then 1.00012 as the window's size changed. Below 1, `ScaleAllSizes` truncates every 1 px border to 0, and Dear ImGui's `CloseButton` draws its cross `(int)_MainScale` pixels thick, which is 0. The running editor lost every close button and every field and button outline, and its paddings shrank by a pixel. The menu bar and first row, live at 0.99986 and from a fresh capture at 1.00012:

![Live at 0.99986: no close buttons or outlines](editor-theme-scales/wayland-live-150-top.png)

![Capture at 1.00012](editor-theme-scales/wayland-capture-150-top.png)

`ImGuiLayer` now rounds the reading to a whole percentage, so both readings are 1 and a resize no longer rebuilds the style, and clamps the scale to 1 to 4 instead of 0.5 to 4: no desktop reports a scale below 1, and Dear ImGui cannot draw the theme there ([Display scale](../editor.md#display-scale)). `ui_tests` checks both readings and a value below 1. The fix was not run live at 150 % again.

## Not checked on Linux

- **A change of scale while running on X11.** The scaling factor is fixed for the process. On native Wayland the switches above changed the density, not the editor's scale; `ui_tests` (the layer's scale read replaced) exercises a change of the scale itself.
- **A panel undocked to a monitor of another scale.** One monitor.
- **Wayland at 125 % and 175 %.**

## Windows

**Not checked yet.** [#418](https://github.com/Pacheco95/sonnet/issues/418) asks the Windows agent for the same four captures at 100, 125, 150 and 200 %, a live change of the display scale, and a panel dragged to a monitor of another scale where there are two.

## macOS

**Not checked yet.** [#419](https://github.com/Pacheco95/sonnet/issues/419) asks the Mac agent for the four captures on a Retina display (the layout should not change, at twice the pixels) and a non-Retina one if available, the window moved between them, and a floating panel reopened. The pixel density division in `ImGuiLayer` has still not run on a Mac.

## Defects

| Found | Where | State |
|---|---|---|
| A `layout.ini` saved at one scale opens cramped at another | X11, 2 | Filed, [#420](https://github.com/Pacheco95/sonnet/issues/420) |
| The viewport renders at half resolution at pixel density 2 | Wayland, 200 % (and expected on Retina) | Filed, [#422](https://github.com/Pacheco95/sonnet/issues/422) |
| Close buttons and outlines lost at a fractional scale (scale read as 0.99986) | Wayland, 150 % | Fixed in this PR: the scale is rounded and clamped to 1 to 4 |

Nothing else was wrong at the scales checked.
