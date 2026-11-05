# A maximized SDL window keeps the old DPI's frame (Windows)

[Issue 423](https://github.com/Pacheco95/sonnet/issues/423), run on 2026-11-02.
Windows 11 Pro 10.0.26200, one 2560x1440 monitor (AW2725DF), SDL 3.4.16 from vcpkg, per-monitor v2 DPI
awareness (SDL's default). No engine code is involved: `repro.c` is plain SDL3 and Win32.

## Result

A window maximized at 100 %, taken to 150 %, 200 % and back to 100 %, keeps the 200 % frame after the last step:

| Step | Window rectangle | Maximized rectangle at that DPI | Client area |
|---|---|---|---|
| start, 100 % | (-8,-8)-(2568,1400) | (-8,-8)-(2568,1400) | 2560x1369 |
| 150 % | (-11,-11)-(2571,1379) | same | 2560x1334 |
| 200 % | (-13,-13)-(2573,1357) | same | 2560x1299 |
| back to 100 % | **(-13,-13)-(2563,1373)** | (-8,-8)-(2568,1400) | **2560x1347** |

These are the editor's numbers in the issue, to the pixel. Windows still has the window maximized (`IsZoomed`) and so
does SDL (`SDL_WINDOW_MAXIMIZED`). The title bar is 5 px off the top of the screen and a 22 px strip above the taskbar
is not covered.

On `SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED` of the maximized window, the program tried each of these:

| Mode | What it does | After 200 → 100 % |
|---|---|---|
| `none` ([output](run-none.txt)) | nothing | stale |
| `remaximize` ([output](run-remaximize.txt)) | `SDL_MaximizeWindow` | stale: Windows ignores `SW_MAXIMIZE` on a maximized window |
| `placement` ([output](run-placement.txt)) | `SetWindowPlacement` with its own placement, `SW_SHOWMAXIMIZED` | stale |
| `restore` ([output](run-restore.txt)) | `SDL_RestoreWindow`, then `SDL_MaximizeWindow` | correct |

The engine did what `restore` does until SDL 3.4.20 fixed the handler ([platform.md](../../platform.md#window), [issue 428](https://github.com/Pacheco95/sonnet/issues/428)).

## Cause

SDL's `WM_DPICHANGED` handler (`src/video/windows/SDL_windowsevents.c`), for a change SDL did not cause itself, calls
`SetWindowPos` with the top-left corner of the rectangle Windows suggests and a frame sized for the window's client
area at the new DPI. It does this whether or not the window is maximized. Going up in scale the result happens to be
the maximized rectangle. Going down, Windows suggests the old corner (-13,-13), and the size SDL computes from the
client area does not reach the work area's edges, so the window ends up neither maximized in shape nor restored. An
upstream fix would leave a maximized window to Windows' own layout, or re-apply the maximized rectangle.

## Running it

From a Developer Command Prompt, with SDL3 from the engine's vcpkg tree (`build/windows-debug/vcpkg_installed/x64-windows`)
and `SDL3.dll` copied next to the program:

```bat
set V=build\windows-debug\vcpkg_installed\x64-windows
cl /W4 /I%V%\include repro.c /link /LIBPATH:%V%\lib SDL3.lib user32.lib /SUBSYSTEM:CONSOLE
cl /W4 setdpi.c /link user32.lib
```

`repro [none|remaximize|restore|placement] [seconds]` opens the maximized window and logs each check. Change the scale
in Settings while it runs, or with `setdpi 150`, `setdpi 200`, `setdpi 100`. `setdpi` sets the primary monitor's scale
through the DisplayConfig request types the Settings app uses (-3 to read, -4 to write; they are undocumented). With no
argument it prints the current scale, so you can put it back afterwards. The runs here waited 3 s after start and 4 s
after each change.

## The editor with the workaround

The same steps with `sonnet_editor` (Debug, this branch), its window read from outside the process:

| Step | Maximized | Restored |
|---|---|---|
| start, 100 % | (-8,-8)-(2568,1400), client 2560x1369 | (-6,14)-(1286,690), client 1276x637 |
| 150 % | (-11,-11)-(2571,1379), client 2560x1334 | (-6,14)-(1292,707), client 1276x637 |
| 200 % | (-13,-13)-(2573,1357), client 2560x1299 | (-6,14)-(1296,722), client 1276x637 |
| back to 100 % | (-8,-8)-(2568,1400), client 2560x1369 | (-6,14)-(1286,690), client 1276x637 |

The maximized window fills the work area with its title bar on screen at every step, and the restored window keeps its
place and its client size.

## SDL main

The same program built against SDL `main` at 940188b2 (3.5.0, 2026-11-02), which includes the later
`WM_DPICHANGED` change of [libsdl-org/SDL#16516](https://github.com/libsdl-org/SDL/pull/16516), gave the same rectangles
in all four modes: stale after 200 → 100 % except with `restore`.

Reported upstream as [libsdl-org/SDL#16641](https://github.com/libsdl-org/SDL/issues/16641).
