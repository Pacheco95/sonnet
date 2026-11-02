# The themed editor at each display scale

[Issue 392](https://github.com/Pacheco95/sonnet/issues/392), the last check of [ADR-0036](../decisions/0036-dear-imgui-stays-the-editor-ui.md): the finished editor (the [Graphite](editor-theme.md) theme, the scale-derived style of #390, the first window and layouts of #410) at the scales it supports, on the platforms it ships on. Status: Linux checked on 2026-11-02 at `fea087e`; Windows and macOS are with device agents ([#418](https://github.com/Pacheco95/sonnet/issues/418), [#419](https://github.com/Pacheco95/sonnet/issues/419)) and **not checked** yet.

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

`SDL_VIDEODRIVER=wayland`, the same captures' `basic` run. GNOME's monitor scale is 100 % here.

| Scale | Image | Result |
|---|---|---|
| 1 (monitor at 100 %) | 1600x900 | Passed. The scale logged is 1 and the capture matches X11 at 1. The only warning is the window icon (`xdg_toplevel_icon_v1` is not supported by GNOME 46), which is unrelated. |
| Monitor at 125 % to 200 % | | **Not checked.** It needs the desktop's own scale changed, and a nested or headless `gnome-shell` would compete with the running session's shell. The expected result is the same layout as at 100 %, drawn at the monitor's density. |

![Wayland at 100 %](editor-theme-scales/wayland-basic-100.jpg)

## Not checked on Linux

- **A change of scale while running.** The machine has one monitor, and on X11 the scaling factor is fixed for the process. Only `ui_tests` (the layer's scale read replaced) exercises a change of scale while running.
- **A panel undocked to a monitor of another scale.** One monitor.

## Windows

**Not checked yet.** [#418](https://github.com/Pacheco95/sonnet/issues/418) asks the Windows agent for the same four captures at 100, 125, 150 and 200 %, a live change of the display scale, and a panel dragged to a monitor of another scale where there are two.

## macOS

**Not checked yet.** [#419](https://github.com/Pacheco95/sonnet/issues/419) asks the Mac agent for the four captures on a Retina display (the layout should not change, at twice the pixels) and a non-Retina one if available, the window moved between them, and a floating panel reopened. The pixel density division in `ImGuiLayer` has still not run on a Mac.

## Defects

| Found | Where | State |
|---|---|---|
| A `layout.ini` saved at one scale opens cramped at another | X11, 2 | Filed, [#420](https://github.com/Pacheco95/sonnet/issues/420) |

Nothing else was wrong at the scales checked.
