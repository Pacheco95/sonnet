# Issue 193: macOS text validation

Validated `feat/gui-text` at `1bf3cdb` on 2026-10-12. Apple silicon Mac, macOS 26.7.1. No engine edits, engine commits or merge.

1. **PASS — Build and tests.** Configured and built Debug using `macos-debug-local`, per the machine's compiler guidance (Xcode Apple Clang 17 instead of Homebrew LLVM). All 15 CTest entries pass. Tests use the SDK Vulkan loader and MoltenVK 1.4.1 with Khronos validation. Renderer: 34,934 assertions / 95 cases; GUI: 244 assertions / 25 cases; runtime: 418 assertions, 23 passed cases and 2 skipped. Skips are the menu and showcase Lavapipe-only goldens. No GPU-device skips. See `tests.log` and `test-details.log` for all module counts.
2. **PASS — Menu capture.** `menu-1080.png` is 1920×1080, six settle frames. Play, Options and Quit have centred white text; all glyphs are visible, with no solid blocks. Still captures do not establish absence of temporal flicker over a continuous run.
3. **PASS for glyph rendering; log caveat.** `menu-360.png` is 640×360. Edges are smooth at both resolutions; smaller labels remain readable. R8/fwidth renderer GPU tests pass with validation. The bundled player uses MoltenVK 1.4.2, reports validation unavailable, and emits the already documented R32_UINT integer-attachment blending warning (see repository `docs/reports/moltenvk-id-blending.md`). No text-related MoltenVK errors were reported. Player captures therefore do not establish validation-layer silence on 1.4.2.
4. **PASS — Same-frame text change.** The focused runtime GPU test passes, including its same-frame glyph count and pixel-change assertions (`label-test.log`). A standalone copy of its `labelProject`/`label.lua` fixture also runs through the bundled player: `label-before.png` shows Score 0 and `label-after.png` shows Score 12. Captures use ten and eleven fixed play steps, respectively; the screenshot is drawn in the following simulation frame, so the second captures the twelfth-frame change.
5. **PASS — Timing recorded.** Debug capture means below; these are short correctness runs, not isolated steady-state benchmarks. Thermal state stayed nominal.

| Capture | Samples | CPU/frame | Frame interval | GPU total | overlay |
|---|---:|---:|---:|---:|---:|
| 1920×1080 | 21 | 2.69 ms | 11.59 ms | 2.283 ms | 0.022 ms |
| 640×360 | 36 | 1.56 ms | 9.70 ms | 1.190 ms | 0.014 ms |

The full per-pass frame-times lines are in `menu1080.log` and `menu360.log`. Logs redact home paths, hardware names and identifiers. Tests intentionally exercise error handling and thus contain expected diagnostic messages; those are distinct from failing tests.
