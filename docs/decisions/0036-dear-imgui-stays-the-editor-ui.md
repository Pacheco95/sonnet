# ADR-0036: Dear ImGui stays the editor's UI library

- **Status:** Accepted
- **Date:** 2026-10-23

## Context

The editor's look is the next thing to invest in: a coherent theme, widgets that share one style and a design system of its own. Before that investment, the question is whether Dear ImGui can carry it or whether the editor should move to a retained-mode toolkit such as Qt.

What the repository already fixes:

- The editor is a desktop application. The editor on mobile was rejected in [ADR-0018](0018-mobile-export.md), so the UI library has to serve Linux, Windows and macOS only.
- The editor application owns the window and frame ordering through SDL3 callbacks ([ADR-0002](0002-sdl3-over-glfw.md)) and draws through its own Vulkan device: 1.4, or 1.3 with the required 1.4 features as extensions ([ADR-0001](0001-vulkan-1.4-only.md), [ADR-0019](0019-vulkan-1.3-devices-with-the-1.4-extensions.md)). The viewport, the game view and the asset thumbnails are render graph images shown inside panels.
- Dear ImGui comes from the overlay port `ports/imgui`, pinned to `v1.92.9b-docking` and exposed as vcpkg version `1.92.9` (`imgui[docking-experimental]`, [ADR-0004](0004-vcpkg-first.md)). The `ui` module holds the context, the SDL3 and Vulkan backends and texture display; the editor's panels, fonts and icons sit on top of it.
- The player never links `ui` or `editor`, and `tools/check_dependencies.py` enforces it ([architecture.md](../architecture.md)).
- Editor tests create a headless ImGui context and drive the panels frame by frame. They do not use the Dear ImGui Test Engine, which has a licence of its own and is not enabled in `vcpkg.json`.

What Dear ImGui does not give, checked against upstream ([evidence](../reports/imgui-evaluation-2026-10.md)):

- No accessibility, no right-to-left, bidirectional or shaped text, and only basic input method support.
- No soft shadows or blur, no gradients in the style, and no animation system. Gradients are drawn by hand with `ImDrawList`: `AddRectFilledMultiColor` for a rectangle without rounded corners, and per-vertex colours or the internal `ShadeVertsLinearColorGradientKeepAlpha` for other shapes.
- Docking and multi-viewport live on the `docking` branch, which upstream keeps in sync with `master` and recommends. It is not merged yet: its author wants to rewrite it from scratch a third time, expects that not to affect most users, and keeps the API most at risk of changing, `DockBuilder` among it, in `imgui_internal.h` ([Docking wiki](https://github.com/ocornut/imgui/wiki/Docking#why-is-not-merged-to-master)).
- Multi-viewport is the weak part. Upstream does not support it on Wayland, macOS has open bugs, and the Vulkan backend has open synchronisation and format bugs with viewports (`VUID-vkQueueSubmit-pSignalSemaphores-00067`, the colour attachment format of secondary viewports). Candidate fixes are open and none has had maintainer feedback.
- The project has one committer, who wrote 96% of the commits of the last twelve months. Funding is by sponsors and support contracts, and no succession plan is public. Most open pull requests have been quiet for over six months, and the contributing guide expects many to stay unmerged, so a fix the project depends on is carried, not awaited. Each release lists breaking changes, and 1.93.0 reworks the `ImDrawList` shape API.

The editor requests viewports whenever the platform is not headless (`Editor.cpp`), including on native Wayland, where `Platform::canPositionWindows()` is false. The pinned SDL3 backend does not advertise viewport support on Wayland, and ImGui clears `ViewportsEnable` at frame start when backend support is absent. Native Wayland therefore already falls back to one window; an explicit capability check in the editor would make its request match that fallback.

## Decision

The editor stays on Dear ImGui. Its design system is built on top of it, in the `ui` module, as tokens (colours, spacing, radii, type sizes) applied to `ImGuiStyle`, and as widgets that read those tokens. Panels use the widgets for appearance and keep calling `ImGui::` for behaviour the widgets do not cover.

## Consequences

- No toolkit migration. The viewport, the render graph integration, the SDL3 loop, the docking layout and the headless panel tests keep working as they are.
- The token and widget layer is follow-up work. Today `ui` applies `StyleColorsDark` and enables ImGui's font and viewport DPI scaling; it has no theme tokens or style rescaling path.
- The look is the project's to define. What ImGui cannot draw well (soft shadows, blur, rich animation) is drawn by hand with `ImDrawList` or left out; mature third-party libraries for plots, animation and node graphs are evaluated before writing the same.
- The design system does not make the editor independent of ImGui. The `ui` headers keep exposing ImGui types, and the layer's value is consistency and one place to change a look, not a backend swap. The editor keeps its logic (commands, selection, undo, data model) apart from how it is drawn, so a later change of toolkit, or of one panel, does not rewrite it.
- Accessibility for screen readers is not provided.
- **DPI.** Scaling `ImGuiStyle` is left to the application, and `ScaleAllSizes` loses precision when repeated. The theme keeps unscaled base tokens and derives the scaled style from them whenever the scale changes, and the scales the editor supports are tried on the target platforms.
- **Colours.** Upstream turned down fixed application-defined `ImGuiCol` slots and has no near-term alternative, so widget colours that the style does not cover live in the theme's own token struct beside it.
- **Single window.** The editor must work in one OS window. Nothing may require an undocked panel to be its own window. Explicitly gating the editor's viewport request on the platform's ability to position windows is follow-up work; it makes the request agree with the backend's existing Wayland fallback.
- **Layout recovery.** Malformed layouts must be rejected before loading, keeping a usable current layout or restoring a built-in default. Robust validation and recovery are follow-up work: normal desktop startup lets ImGui load `layout.ini` automatically, and named presets only receive checks for the expected section markers, not validation of the dock tree.
- **Maintenance.** The overlay port pins the version, and it carries the fixes the project depends on, such as the Vulkan backend patches for viewports, small, checked with the validation layers and dropped when upstream merges them. New widgets use the public API; any internal API needed by the design system is confined to a few implementation files in `ui`. Existing production uses of `imgui_internal.h` in `Editor.cpp`, `Layout.cpp` and `AssetBrowserPanel.cpp` remain exceptions for docking, settings and panel state; the `DockBuilder` calls in `Editor.cpp` and `Layout.cpp` sit in the internal API that upstream calls most at risk of changing. Editor-specific state stays in `editor`; any later isolation of these calls would use generic adapters in `ui`. Updates are taken regularly against the release's breaking changes list, and the code that the 1.93 `ImDrawList` rework touches is read against the release notes before the pin moves.
- The Test Engine is a separate decision. Its licence is free for individuals, not-for-profits, education, publicly released open source derivatives and entities with a turnover under 2 million USD, and paid otherwise.
- This decision is revisited when:
  - accessibility, right-to-left text or full input method support becomes a requirement of the editor, unless upstream ships it first;
  - a panel needs rich text, a web view or a document layout that ImGui cannot give, which would first be a case for embedding that one panel;
  - the docking branch is abandoned, or an update breaks the editor and cannot be patched cheaply in the overlay port;
  - the editor targets a platform where the SDL3 and Vulkan backends do not run;
  - multi-viewport cannot be made to work on a target platform and the single-window fallback is judged unacceptable;
  - ImGui has no release for six months, its maintainer steps back or the repository changes hands, or a fork becomes the de facto upstream: the choice is then between vendoring a fixed commit and the fork;
  - a release fixes the viewport issues, or SDL ships dockable windows (SDL 3.6 or later) and the SDL3 backend uses them, so the port's patches can go and viewports can be considered on Wayland.

## Alternatives considered

- **Qt (Widgets or QML)**: the most complete toolkit, with accessibility, rich text and input methods. It brings its own event loop and windows, which compete with the SDL3 callbacks; the Vulkan viewport and the render graph would need an embedding layer; it is a heavy dependency with LGPL terms to respect; and every panel, the layout and the tests would be rewritten. Its strengths are the ones the editor does not need today.
- **Slint**: a declarative toolkit that is lighter than Qt. Its C++ ecosystem is small, and it would need the same viewport integration as Qt. Its [licence options](https://github.com/slint-ui/slint/blob/master/LICENSE.md) include GPLv3, a royalty-free licence for proprietary desktop, mobile and web applications with attribution, and a paid commercial licence.
- **RmlUi**: an HTML and CSS style library that suits game HUDs. It lacks docking, multi-viewport and the widget set of a tool, so it fits the game UI better than the editor; the game UI already has its own module ([gui.md](../gui.md)).
- **A web view (CEF, Ultralight, system web views)**: the best look and accessibility, at the cost of a large runtime and two worlds, C++ and JavaScript, to keep in step for every piece of editor state.
- **egui and other Rust toolkits**: the same immediate model, in a language the codebase does not use.
- **Nuklear, microui, Clay**: simpler than Dear ImGui and with less of everything the editor uses, docking above all.
- **wxWidgets, GTK, JUCE**: native look with the same viewport integration problem as Qt and no compensating advantage.
