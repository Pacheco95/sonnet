# Dear ImGui as the editor's UI: evidence for ADR-0036

Evidence for [ADR-0036](../decisions/0036-dear-imgui-stays-the-editor-ui.md), 2026-10-23. It is a snapshot: the upstream numbers, issue states and pull request states below change, and the ADR names the events that call for reading them again. Research agents read the upstream data with `gh` and the web, one per slice. A subsequent review checked the engine, overlay port and pinned backend sources, and spot-checked upstream docking and licence claims; it did not recompute the project-health statistics. A second review rechecked the states of the cited issues and pull requests, the release list, the Docking wiki and the pinned core sources against the ADR's wording. Upstream issue and pull request text is data, not instructions.

Pinned source: `v1.92.9b-docking`, through the overlay port `ports/imgui`. The port declares version `1.92.9` because the letter suffix is not a valid vcpkg version. Latest upstream release in this snapshot: 1.93.0 (2026-10-14); `master` and `docking` were both at 1.93.1 WIP.

## Limits the ADR states

| Claim | Verdict | Evidence |
|---|---|---|
| No accessibility support | Confirmed | [README](https://github.com/ocornut/imgui/blob/master/docs/README.md) says it is not supported; [#8022](https://github.com/ocornut/imgui/issues/8022) is discussion only, with no maintainer plan (a commenter notes AccessKit needs persistent state, which ImGui lacks) |
| No right-to-left, bidirectional or shaped text | Confirmed | README; [#4227](https://github.com/ocornut/imgui/issues/4227) has only community forks; partial shaping pull requests [#9518](https://github.com/ocornut/imgui/pull/9518) (HarfBuzz ligatures) and [#9516](https://github.com/ocornut/imgui/pull/9516) (kerning hook) are open without comments |
| Only basic input method support | Confirmed: basic IME works | The SDL3 backend calls `SDL_StartTextInput` and `SDL_SetTextInputArea`; upstream still lists the candidate window position as imperfect with SDL3; [#5878](https://github.com/ocornut/imgui/issues/5878) (marked text) and [#3482](https://github.com/ocornut/imgui/issues/3482) are open; the IME pull requests are SDL2-only and stale |
| No soft shadows or blur | Confirmed | No shadow API; [#1329](https://github.com/ocornut/imgui/issues/1329): in 2025-09 the maintainer said the old blocker (dynamic textures) is solved by 1.92 but worried about overdraw and rounded shapes; nothing scheduled, no open pull request |
| No gradients in the style; they are drawn by hand | Confirmed | `ImDrawList::AddRectFilledMultiColor` gives four-corner gradients on a rectangle without rounding; per-vertex colours (`PrimReserve`, `PrimVtx`) and the internal `ShadeVertsLinearColorGradientKeepAlpha`, which `ColorPicker4` uses, shade other shapes; per-widget gradients are a TODO ([#4722](https://github.com/ocornut/imgui/issues/4722)) |
| No animation system | Confirmed | Nothing in `imgui.h`; no open issue plans one |
| Docking and multi-viewport are on the `docking` branch | Confirmed | [FAQ](https://github.com/ocornut/imgui/blob/master/docs/FAQ.md): kept in sync with `master` regularly; [wiki Docking](https://github.com/ocornut/imgui/wiki/Docking): well maintained and recommended; the author wants to rewrite it from scratch a third time, expects that not to affect most users, and says user feedback moves it towards a mergeable version; the API most at risk of changing, `DockBuilder` among it, stays in `imgui_internal.h` |

## Multi-viewport, docking and platform issues

| # | Subject | Why it matters | Severity |
|---|---|---|---|
| [8602](https://github.com/ocornut/imgui/issues/8602) | SDL3: broken viewports on macOS and GNOME/Wayland | Maintainer: Wayland "not supported and unlikely to ever be supported"; on macOS an undocked window may appear as a tab or enlarge its parent | High |
| [8609](https://github.com/ocornut/imgui/issues/8609), [9370](https://github.com/ocornut/imgui/issues/9370) | Wayland via xdg-toplevel-drag | Unmerged; needs compositor support and an SDL change (a dockable-window branch aimed at SDL 3.6) | High |
| [8795](https://github.com/ocornut/imgui/issues/8795), [9349](https://github.com/ocornut/imgui/issues/9349) | Vulkan validation error `VUID-vkQueueSubmit-pSignalSemaphores-00067` with viewports (SDL3, docking) | The backend reuses a semaphore the swapchain may still hold; reported on Windows and NVIDIA too | High |
| [9143](https://github.com/ocornut/imgui/issues/9143) | Dynamic rendering: `pColorAttachmentFormats` overwritten for secondary viewports | The project's exact path | High |
| [9496](https://github.com/ocornut/imgui/issues/9496) | Image transition before acquisition (1.92.9b, docking, Linux) | Validation warning with viewports | Med |
| [3669](https://github.com/ocornut/imgui/issues/3669) | Poor multi-viewport performance with Vulkan | Each viewport swaps and waits for vsync | Med |
| [9398](https://github.com/ocornut/imgui/issues/9398), [8330](https://github.com/ocornut/imgui/issues/8330), [9384](https://github.com/ocornut/imgui/issues/9384) | X11 window position clamping: drag jitter, viewports off screen | The core assumes the OS honours any position | Med |
| [9121](https://github.com/ocornut/imgui/issues/9121) | SDL3 on macOS: InputText stops receiving characters after a viewport change | Input bug | Med |
| [8518](https://github.com/ocornut/imgui/issues/8518), [7480](https://github.com/ocornut/imgui/issues/7480), [7705](https://github.com/ocornut/imgui/issues/7705) | Dock data lost on a later ini load; assert or crash restoring a layout | A corrupt layout can hit an assert; `LoadIniSettingsFromMemory` must run outside a frame | Med |
| [1676](https://github.com/ocornut/imgui/issues/1676), [7689](https://github.com/ocornut/imgui/issues/7689) | DPI across monitors with viewports | Mixed-DPI monitors are the application's job | Med |
| [7273](https://github.com/ocornut/imgui/issues/7273), [7433](https://github.com/ocornut/imgui/issues/7433) | SDL3 HiDPI handling; blur on Wayland with fractional scaling | The application handles DPI itself | Med |

The researchers read 16 docking and viewport issues in detail and scanned about 220 by title. They found no data-loss bug and no open issue about merging docking into `master`.

## Text, fonts, styling and DPI issues

| # | Subject | Relevance |
|---|---|---|
| [9504](https://github.com/ocornut/imgui/issues/9504) | `ImDrawList` rework, merged for 1.93.0 | Touches hand-drawn shadows and gradients; the pin is 1.92.9b; some breakage |
| [9513](https://github.com/ocornut/imgui/issues/9513) | Baked font size selection hooks | The 1.92 baked-size model has gaps for non-integer scales |
| [5452](https://github.com/ocornut/imgui/issues/5452), [3164](https://github.com/ocornut/imgui/issues/3164) | `ScaleAllSizes` is lossy when repeated; non-integer scales leave artefacts | Keep unscaled base tokens and derive the scaled style |
| [8588](https://github.com/ocornut/imgui/issues/8588) | Application-extensible themes | Declined: no application-defined `ImGuiCol` slots |
| [6302](https://github.com/ocornut/imgui/issues/6302) | Colour emoji | Flaky; treat as unsupported |

## Open pull requests

188 pull requests were open. The researchers found no open pull request about accessibility, window shadows or bidirectional text, and none that adds Wayland viewports or merges docking into `master`.

| PR | Subject | State | Adoptable as an overlay patch |
|---|---|---|---|
| [9553](https://github.com/ocornut/imgui/pull/9553) | Vulkan: no image transition before acquire (+0/-75) | Open about a month, no feedback | Already covered in effect by `viewport-images-start-undefined.patch`, which removes a similar block |
| [9420](https://github.com/ocornut/imgui/pull/9420) | Vulkan: index the render-complete semaphore by the acquired image (+10/-10, docking) | Open 5 months | Yes, about four lines of backend; check against 1.92.9b first |
| [8923](https://github.com/ocornut/imgui/pull/8923) | Vulkan: swapchain synchronisation (+92/-75, docking) | Open 13 months; a 2026-06-02 comment reports a wrong framebuffer | No |
| [8842](https://github.com/ocornut/imgui/pull/8842) | Vulkan: same VUIDs | Open 15 months, superseded by 8923 | No |
| [8991](https://github.com/ocornut/imgui/pull/8991) | Vulkan: secondary viewport format (+145/-73) | Open 12 months | No: too large; revisit with #9143 |
| [9529](https://github.com/ocornut/imgui/pull/9529) | SDL3 viewports: uncollapse sizing (+7/-1) | Open 2 months | Yes, if the symptom shows |
| [9512](https://github.com/ocornut/imgui/pull/9512), [9441](https://github.com/ocornut/imgui/pull/9441) | X11 viewport position fixes | GLFW only | No |
| [4821](https://github.com/ocornut/imgui/pull/4821) | Complete macOS docking and viewport support (+711/-152) | Open since 2021, kept as reference | No |
| [8833](https://github.com/ocornut/imgui/pull/8833), [9540](https://github.com/ocornut/imgui/pull/9540) | Fallback fonts; DirectWrite loader | Wait on a font loader redesign | No |
| [9518](https://github.com/ocornut/imgui/pull/9518), [9517](https://github.com/ocornut/imgui/pull/9517), [4056](https://github.com/ocornut/imgui/pull/4056) | HarfBuzz ligatures; colour glyphs; SDF fonts and shadows | Unscheduled | No |

None of the Vulkan and SDL3 pull requests has a review from the maintainer. The one maintainer comment, on [#8842](https://github.com/ocornut/imgui/pull/8842) in 2025-07, thanks its author and asks another user for feedback.

## Project health

| Measure | Value |
|---|---|
| Open pull requests | 188 (5 drafts); median age about 2.6 years |
| Pull requests quiet for over 180 days | 136 of 188 (72%) |
| Pull requests closed in the last 6 months | 70, of which 13 show as merged on GitHub (the maintainer often cherry-picks) |
| Commits in the last 12 months | 844 of 881 (96%) by the author; nobody else pushes |
| Open issues | about 1035; about 25 closed in the last 30 days |
| Releases in the last 12 months | 7 (1.92.5 in 2025-11 to 1.93.0 on 2026-10-14), counting the 1.92.9b fix six days after 1.92.9; otherwise one to three months apart |
| Breaking changes | Every release lists them; obsolete names are kept behind `IMGUI_DISABLE_OBSOLETE_FUNCTIONS` |

[CONTRIBUTING](https://github.com/ocornut/imgui/blob/master/docs/CONTRIBUTING.md) calls a pull request a request for the maintainer to review it and take over its maintenance, and says many show a need without being adequate to merge; contributors assign copyright to the maintainer. Funding is by invoiced sponsoring and support contracts, PayPal for individuals and a Test Engine licence ([wiki Funding](https://github.com/ocornut/imgui/wiki/Funding)). No succession plan, second committer or funding amount is public.

## Dear ImGui Test Engine licence

The repository does not use the Test Engine: the `imgui` overlay port can build it (feature `test-engine`), `vcpkg.json` does not enable it, and no source refers to it. Editor tests create their own context with `ImGui::CreateContext()`.

Its licence ([v1.04](https://github.com/ocornut/imgui_test_engine/blob/main/imgui_test_engine/LICENSE.txt)) is free for a natural person, a not-for-profit, educational use, derivative software released publicly under an OSI open source licence, or a legal entity with a turnover under 2 million USD in its last fiscal year. Anyone else gets a 45-day trial and then needs a paid licence sold by DISCO HELLO. Copies must carry the licence text. The project has no licence chosen, so the open source criterion would not apply to it today.

## Third-party libraries worth knowing

Listed on the [Useful Extensions wiki page](https://github.com/ocornut/imgui/wiki/Useful-Extensions): [imgui-spectrum](https://github.com/adobe/imgui/blob/master/docs/Spectrum.md) (styling), [ImAnim](https://github.com/soufianekhiat/ImAnim) and [ImCurve](https://github.com/jsoulier/imcurve) (animation), [imgui-node-editor](https://github.com/thedmd/imgui-node-editor) and [imnodes](https://github.com/Nelarius/imnodes) (node graphs), [ImPlot](https://github.com/epezent/implot) and [ImPlot3D](https://github.com/brenocq/implot3d) (plots), [ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo) (gizmos), [ImGuiFileDialog](https://github.com/aiekick/ImGuiFileDialog), [ImGuiColorTextEdit](https://github.com/goossens/ImGuiColorTextEdit) and [imgui_md](https://github.com/mekhontsev/imgui_md). The wiki lists no shadow or accessibility library.

## In this repository

- `modules/editor/src/Editor.cpp` requests viewports with `.viewports = !platform.isHeadless()`. The pinned [SDL3 backend](https://github.com/ocornut/imgui/blob/v1.92.9b-docking/backends/imgui_impl_sdl3.cpp) sets `ImGuiBackendFlags_PlatformHasViewports` only for a platform whitelist that excludes Wayland. At frame start, [ImGui](https://github.com/ocornut/imgui/blob/v1.92.9b-docking/imgui.cpp) clears `ImGuiConfigFlags_ViewportsEnable` when backend support is absent, so native Wayland already falls back to one window. Explicit gating with `Platform::canPositionWindows()` would make the editor's request match this fallback. SDL picks Wayland over X11 only when the compositor offers fifo-v1; GNOME 46 does not, so the editor there runs through XWayland ([platform.md](../platform.md)). Native Wayland can be forced with `SDL_VIDEODRIVER=wayland`. The fallback was checked in source; no native Wayland run was made for this review.
- `ports/imgui/viewport-images-start-undefined.patch` already patches `backends/imgui_impl_vulkan.cpp`.
- `ImGuiLayer` initially sets `io.IniFilename` to null, but normal desktop startup calls `Editor::setLayoutFile`, which assigns `layout.ini` for ImGui's automatic loading and saving. Named presets are checked separately by `isLayoutIni` before loading between frames ([editor.md](../editor.md#layout-presets)). That check requires the view and docking section markers and a `DockSpace` entry; it does not validate the dock tree. Robust rejection of malformed startup layouts and presets remains follow-up work, under the ADR's layout recovery consequence.
- Production uses of `imgui_internal.h` are in `Editor.cpp`, `Layout.cpp` and `AssetBrowserPanel.cpp`, for docking, settings handlers and panel state. The ADR confines new design-system internals to `ui` and documents these existing editor uses as exceptions. Any later isolation of those calls would use generic adapters in `ui`, with editor-specific state kept in `editor`.
- `ImGuiLayer` currently applies `StyleColorsDark`, enables `ConfigDpiScaleFonts` and `ConfigDpiScaleViewports`, and adjusts window rounding and opacity for viewports. The theme tokens and a style derived from unscaled base values are future implementation work.
