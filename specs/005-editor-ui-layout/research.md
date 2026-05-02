# Research: Editor UI Layout

**Branch**: `005-editor-ui-layout` | **Date**: 2026-05-02

## Decision 1: UI Library

**Decision**: Dear ImGui (docking branch) with `imgui_impl_sdl3` and `imgui_impl_vulkan` backends.

**Rationale**: ImGui is the de-facto standard immediate-mode UI for C++ game engine editors. It natively supports the SDL3 and Vulkan backends already present in the project. Its built-in dockspace (`ImGuiConfigFlags_DockingEnable`) provides the panel drag-to-dock, tab grouping, floating windows, and resize behavior required by the spec with no additional code. It is a single-header-style C++ library compatible with C++23 and CMake FetchContent. CPU/memory overhead is negligible (< 5 MB RAM, < 1% CPU at idle).

**Alternatives considered**:
- Qt: Requires a separate framework installation, LGPL licensing constraints, and a fundamentally different build model — disproportionate for an embedded editor.
- wxWidgets: Similar weight to Qt, no game engine adoption, no SDL3/Vulkan integration path.
- Nuklear: Immediate mode, lightweight, but lacks the docking/tabbing system required by the spec.
- Custom: Estimated 4–6x implementation effort with no correctness guarantee; ruled out.

**Pinned version**: The docking branch tag matching the current stable release (e.g., `v1.91.9-docking`). Verify the latest `*-docking` tag at <https://github.com/ocornut/imgui/tags> at implementation time and pin to a specific release tag per Constitution Principle III. Do NOT use `docking` as the GIT_TAG (branch name).

---

## Decision 2: Viewport Rendering Strategy

**Decision**: Scene renders to a Vulkan offscreen framebuffer (separate `VkRenderPass` + `VkImage`). The resulting image is sampled by ImGui via a descriptor set, displayed as `ImGui::Image()` inside the viewport panel.

**Rationale**: Offscreen rendering is the only approach that allows the viewport panel to be freely moved, resized, and tabbed like other panels. It matches the behavior in Unreal, Unity, and Godot editors. ImGui's Vulkan backend provides `ImGui_ImplVulkan_AddTexture()` to register a `VkImageView` + sampler as an `ImTextureID` (a `VkDescriptorSet` cast). The viewport panel queries this handle and passes it to `ImGui::Image()`.

The Vulkan native handles required by ImGui (VkInstance, VkDevice, VkPhysicalDevice, VkQueue, VkRenderPass, etc.) are exposed via a new `RendererNativeHandles` struct (all fields `uint64_t`) on `IRendererBackend::getNativeHandles()`, preserving the opaque-handle pattern required by Constitution Principle I. The `SonnetEditor` module casts these internally to Vulkan types — no Vulkan headers appear in `SonnetEditor`'s public interface.

**Alternatives considered**:
- ImGui overlay on top of the existing swapchain render: viewport panel cannot be independently moved or resized; moving the window shows engine background, not panel content. Rejected for spec non-compliance.
- Separate OS window for the viewport: cross-platform complexity, synchronization overhead, defeats the purpose of a unified editor layout. Rejected.

---

## Decision 3: Layout Persistence Format

**Decision**: Each named layout is stored as a separate ImGui `.ini` file on disk (`layouts/<name>.ini`). The active layout name is tracked in `layouts/active.txt`. Layout enumeration discovers all `*.ini` files in the directory.

**Rationale**: ImGui provides `ImGui::SaveIniSettingsToMemory()` and `ImGui::LoadIniSettingsFromMemory()` which serialize and restore the complete docking state (panel positions, sizes, tab groups, floating window state) as a text-format `.ini` string. Storing each named layout as a separate `.ini` file requires zero additional dependencies (no JSON or XML library), is human-readable and debuggable, and maps directly to ImGui's serialization API. The `active.txt` approach is minimal and sufficient for single-user desktop use.

**Alternatives considered**:
- Single JSON file wrapping multiple layouts: requires adding `nlohmann/json` or a custom parser. The layout content is already text (ImGui ini), making JSON wrapping redundant overhead. Rejected.
- SQLite: Overkill for developer preferences storage. Rejected.
- Binary format: Non-debuggable, no benefit over ini for this use case. Rejected.

---

## Decision 4: Engine Log Integration

**Decision**: A custom `spdlog` sink (`EditorLogSink`) forwards log records to a thread-safe in-memory ring buffer in the log panel. The ring buffer is capped at 10,000 entries. The sink is registered via `spdlog::default_logger()->sinks().push_back(sink)` during editor initialization — no changes to existing `SONNET_LOG_*` macros are required.

**Rationale**: Adding a sink to spdlog's default logger intercepts all log output at the source, capturing file/line metadata already embedded by `SPDLOG_` macros. A ring buffer cap prevents unbounded memory growth during long sessions. The in-memory buffer is accessed from the render thread (ImGui rendering); the spdlog sink may be called from any thread, so the buffer must be protected by a mutex. Auto-scroll pauses when the user scrolls up and resumes when they scroll to the bottom (per spec clarification Q3).

**Alternatives considered**:
- Polling the log file: Filesystem overhead, no metadata for severity-based filtering. Rejected.
- Modifying `SONNET_LOG_*` macros: Breaks the abstraction layer and couples logging to UI. Rejected.
- Separate log channel: Requires duplicating all log call sites. Rejected.

---

## Decision 5: SonnetEditor Module Design

**Decision**: Single new `SonnetEditor` CMake static library at `src/editor/`. ImGui (with SDL3 and Vulkan backends) is a `PRIVATE` dependency of `SonnetEditor`. The public interface exposes only `IEditor`, `IPanel`, `ILayoutManager`, and `EditorFactory` with no ImGui or Vulkan types.

**Rationale**: A single module avoids the proliferation of `SonnetEditorImGui` + `SonnetEditorVulkan` split modules that would add complexity without benefit at this project scale. ImGui as a PRIVATE dependency is architecturally correct — it is a swappable implementation detail per Constitution Principle I. The Vulkan native handles needed for ImGui init flow through `IRendererBackend::getNativeHandles()` (opaque `uint64_t` fields), so `SonnetEditor` never includes Vulkan headers in its public interface.

**Dependency graph** (no cycles, all one-directional):
```
SonnetApp
├── SonnetEditor        [PUBLIC]  ← new
│   ├── SonnetRenderer  [PUBLIC]
│   ├── SonnetWindow    [PUBLIC]
│   ├── SonnetLogging   [PUBLIC]
│   └── ImGui           [PRIVATE] ← new FetchContent dep
├── SonnetRendererVulkan [PRIVATE]
└── SonnetWindow        [PRIVATE]
```

**Alternatives considered**:
- `SonnetEditor` + `SonnetEditorVulkan` split: Mirrors the renderer module split but adds a second module for one feature. The renderer split is justified because `SonnetRenderer` defines a stable interface with multiple potential backends. The editor currently has only one backend (ImGui/Vulkan) and no immediate plan for a second. Rejected as premature abstraction.
