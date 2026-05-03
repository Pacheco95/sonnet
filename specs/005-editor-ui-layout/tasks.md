# Tasks: Editor UI Layout

**Input**: Design documents from `specs/005-editor-ui-layout/`  
**Branch**: `005-editor-ui-layout`  
**Prerequisites**: plan.md ✅ spec.md ✅ research.md ✅ data-model.md ✅ contracts/ ✅ quickstart.md ✅

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies on incomplete tasks in the same phase)
- **[Story]**: User story this task belongs to (US1–US4)
- Exact file paths included in all descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Add ImGui dependency and create the `SonnetEditor` build skeleton. No logic yet.

- [ ] T001 Add Dear ImGui FetchContent declaration (docking branch, pinned release tag) to `cmake/FetchDependencies.cmake`
- [ ] T002 [P] Create `src/editor/CMakeLists.txt` with empty `SonnetEditor` static library target; link `SonnetRenderer`, `SonnetWindow`, `SonnetLogging` PUBLIC and ImGui PRIVATE; apply `sonnet_set_compile_options()`; add ImGui FetchContent headers with `SYSTEM` keyword
- [ ] T003 [P] Add `add_subdirectory(src/editor)` to root `CMakeLists.txt`
- [ ] T004 [P] Create `tests/unit/editor/CMakeLists.txt` with empty Catch2 test executable `SonnetEditorTests` registered via `catch_discover_tests()`
- [ ] T005 Add `add_subdirectory(editor)` to `tests/unit/CMakeLists.txt`
- [ ] T006 [P] Add `EditorLifecycleTest.cpp` stub and register it in `tests/integration/CMakeLists.txt`

**Checkpoint**: `cmake -S . -B build && cmake --build build` succeeds with no new warnings.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Public interfaces, opaque renderer handle struct, and test infrastructure that ALL user stories depend on.

**⚠️ CRITICAL**: No user story work can begin until this phase is complete.

- [ ] T007 [P] Add `RendererNativeHandles` struct and default `getNativeHandles()` method to `src/renderer/include/sonnet/renderer/IRendererBackend.hpp` (all fields `uint64_t`/`uint32_t`, default-returns zero struct)
- [ ] T008 Implement `VulkanBackend::getNativeHandles()` in `src/renderer_vulkan/src/VulkanBackend.hpp` and `src/renderer_vulkan/src/VulkanBackend.cpp` (return `VkInstance`, `VkPhysicalDevice`, `VkDevice`, `VkQueue`, `VkRenderPass`, `VkDescriptorPool`, queue family, image count as `uint64_t` casts)
- [ ] T009 [P] Create `src/editor/include/sonnet/editor/IEditor.hpp` per contracts/editor-interfaces.md (`init`, `shutdown`, `processEvent`, `render`)
- [ ] T010 [P] Create `src/editor/include/sonnet/editor/IPanel.hpp` per contracts/editor-interfaces.md (`title`, `isPermanent`, `draw`)
- [ ] T011 [P] Create `src/editor/include/sonnet/editor/ILayoutManager.hpp` per contracts/editor-interfaces.md (`listLayouts`, `saveLayout`, `loadLayout`, `activeLayoutName`, `restoreLastLayout`, `resetToDefault`)
- [ ] T012 [P] Create `src/editor/include/sonnet/editor/EditorFactory.hpp` per contracts/editor-interfaces.md (`createEditor(layoutsDir)` returning `std::unique_ptr<IEditor>`)
- [ ] T013 Create `tests/unit/editor/EditorInterfaceTest.cpp` with mock `IEditor`, `IPanel`, `ILayoutManager` stubs to confirm all interfaces compile and link; add to `tests/unit/editor/CMakeLists.txt`

**Checkpoint**: `ctest --output-on-failure` passes; interface headers compile cleanly.

---

## Phase 3: User Story 1 — Default Layout on Launch (Priority: P1) 🎯 MVP

**Goal**: Engine launches with a familiar four-panel docked layout; the colored triangle renders in the Viewport panel; all other panels show placeholder content.

**Independent Test**: Run `./build/src/app/Sonnet` → editor window appears with four visible panels (Viewport, Log, Scene Hierarchy, Inspector); triangle renders in Viewport; closing the window exits cleanly.

- [ ] T014 [P] [US1] Add offscreen framebuffer resources to `src/renderer_vulkan/src/VulkanBackend.hpp` and `src/renderer_vulkan/src/VulkanBackend.cpp`: create a separate `VkRenderPass`, `VkImage`, `VkImageView`, `VkFramebuffer`, `VkSampler`, and `VkDescriptorSet` for the offscreen scene render target; add `renderSceneOffscreen()` and `getViewportTextureId() const → uint64_t` methods; move triangle draw calls into the offscreen pass
- [ ] T015 [P] [US1] Create `src/editor/src/log/EditorLogSink.hpp` and `src/editor/src/log/EditorLogSink.cpp`: stub spdlog sink that stores up to 10,000 `LogEntry` items in a thread-safe `std::deque` protected by `std::mutex`; exposes `drain(std::vector<LogEntry>&)` for the log panel to pull entries
- [ ] T016 [P] [US1] Create `src/editor/src/panels/SceneHierarchyPanel.hpp` and `src/editor/src/panels/SceneHierarchyPanel.cpp`: implements `IPanel`; `title()` returns `"Scene Hierarchy"`; `draw()` renders an `ImGui::Text("(empty)")` placeholder
- [ ] T017 [P] [US1] Create `src/editor/src/panels/InspectorPanel.hpp` and `src/editor/src/panels/InspectorPanel.cpp`: implements `IPanel`; `title()` returns `"Inspector"`; `draw()` renders an `ImGui::Text("(empty)")` placeholder
- [ ] T018 [US1] Create `src/editor/src/panels/ViewportPanel.hpp` and `src/editor/src/panels/ViewportPanel.cpp`: implements `IPanel`; `title()` returns `"Viewport"`; `isPermanent()` returns `true`; constructor takes `uint64_t viewportTextureId`; `draw()` queries current `ImGui::GetContentRegionAvail()` size and calls `ImGui::Image(reinterpret_cast<ImTextureID>(viewportTextureId), size)` (depends on T014)
- [ ] T019 [US1] Create `src/editor/src/Editor.hpp` and `src/editor/src/Editor.cpp` implementing `IEditor`: `init()` calls `ImGui::CreateContext()`, sets `ImGuiConfigFlags_DockingEnable`, inits `imgui_impl_sdl3` and `imgui_impl_vulkan` using handles from `IRendererBackend::getNativeHandles()`; builds default dockspace layout (Viewport centre, Log bottom, SceneHierarchy left, Inspector right); registers all four panels; `processEvent()` forwards SDL events to ImGui backends; `render()` calls `ImGui_ImplVulkan_NewFrame()`, `ImGui_ImplSDL3_NewFrame()`, `ImGui::NewFrame()`, renders all panels inside `ImGui::DockSpace()`, then `ImGui::Render()` + `ImGui_ImplVulkan_RenderDrawData()` (depends on T015–T018)
- [ ] T020 [US1] Implement `EditorFactory::createEditor(layoutsDir)` in `src/editor/src/EditorFactory.cpp`: constructs and returns a `std::make_unique<Editor>(layoutsDir)` (depends on T019)
- [ ] T021 [US1] Modify `src/app/main.cpp`: in `SDL_AppInit` create editor via `createEditor(exeDir / "layouts")` and call `editor->init(window, backend)`; in `SDL_AppIterate` call `renderer->renderSceneOffscreen()` then `editor->render()`; in `SDL_AppEvent` call `editor->processEvent(event)` first; in `SDL_AppQuit` call `editor->shutdown()` before renderer/window shutdown; add `IEditor` pointer to `AppState` (depends on T020)
- [ ] T022 [P] [US1] Write `tests/unit/editor/EditorInterfaceTest.cpp`: mock `IEditor` that records `init`/`shutdown` call counts; assert lifecycle (init → render × N → shutdown) without any GPU or display (depends on T019)
- [ ] T023 [US1] Write `tests/integration/EditorLifecycleTest.cpp`: construct `Editor` with a mock `IRendererBackend` returning zeroed `RendererNativeHandles` and a mock `IWindow`; assert `init()` fails gracefully (returns `false`) when ImGui Vulkan init cannot proceed headlessly; assert `shutdown()` is safe to call after a failed `init()` (depends on T020)

**Checkpoint**: Engine launches showing four-panel layout with colored triangle. `ctest --output-on-failure` passes.

---

## Phase 4: User Story 2 — Panel Rearrangement (Priority: P2)

**Goal**: All four panels can be dragged, resized, tabbed, floated, and re-docked. Viewport cannot be closed.

**Independent Test**: Launch editor → drag "Inspector" panel onto "Scene Hierarchy" tab bar → both appear as tabs in one container; drag a tab out → it floats; resize a panel border → adjacent panels adjust; attempt to close Viewport → no close button present.

- [ ] T024 [US2] Configure per-panel `ImGuiWindowFlags` in `src/editor/src/Editor.cpp` and each panel's `draw()` entry: panels without `isPermanent()` get `ImGuiWindowFlags_None`; panels with `isPermanent()` get `ImGuiWindowFlags_NoClose`; dockspace uses `ImGuiDockNodeFlags_None` (allow rearrangement); ensure `ImGui::DockSpace()` is called with a stable dock ID each frame
- [ ] T025 [P] [US2] Enforce minimum panel sizes in `src/editor/src/panels/ViewportPanel.cpp`, `src/editor/src/panels/LogPanel.cpp` (stub), `src/editor/src/panels/SceneHierarchyPanel.cpp`, and `src/editor/src/panels/InspectorPanel.cpp`: call `ImGui::SetNextWindowSizeConstraints({200, 100}, {FLT_MAX, FLT_MAX})` before each `ImGui::Begin()` call; viewport minimum is `{300, 200}`
- [ ] T026 [P] [US2] Add `tests/unit/editor/PanelFlagsTest.cpp`: instantiate each panel type; assert `ViewportPanel::isPermanent()` returns `true`; assert `LogPanel::isPermanent()`, `SceneHierarchyPanel::isPermanent()`, `InspectorPanel::isPermanent()` return `false`; add to `tests/unit/editor/CMakeLists.txt`

**Checkpoint**: All rearrangement behaviors work interactively. `ctest --output-on-failure` passes.

---

## Phase 5: User Story 3 — Save and Load Custom Layouts (Priority: P3)

**Goal**: Users can save named layouts, load them from a scrollable list, and have the last active layout restored on restart.

**Independent Test**: Rearrange panels → Layout > Save Layout… → enter name "MyLayout" → restart engine → Layout > Load Layout… → select "MyLayout" → panels restore to saved positions.

- [ ] T027 [US3] Create `src/editor/src/LayoutManager.hpp` and `src/editor/src/LayoutManager.cpp` implementing `ILayoutManager`: `saveLayout(name)` calls `ImGui::SaveIniSettingsToMemory()` and writes result to `layoutsDir/<name>.ini` + updates `layoutsDir/active.txt`; `loadLayout(name)` reads `<name>.ini` and calls `ImGui::LoadIniSettingsFromMemory()`; `listLayouts()` enumerates `*.ini` via `std::filesystem::directory_iterator`; `restoreLastLayout()` reads `active.txt` and calls `loadLayout()`; `resetToDefault()` calls `ImGui::LoadIniSettingsFromMemory("")`; all failures emit `SONNET_LOG_WARN` or `SONNET_LOG_ERROR` and return `false`
- [ ] T028 [P] [US3] Write `tests/unit/editor/LayoutManagerTest.cpp`: use a `std::filesystem::temp_directory_path()` subdirectory as layouts dir; assert `saveLayout("test")` creates `test.ini`; assert `listLayouts()` returns `{"test"}`; assert `loadLayout("nonexistent")` returns `false`; assert `active.txt` contains `"test"` after save; assert overwrite prompt logic (save same name twice succeeds); add to `tests/unit/editor/CMakeLists.txt`
- [ ] T029 [US3] Add a **Layout** top-level menu to the ImGui menu bar in `src/editor/src/Editor.cpp`: `ImGui::BeginMainMenuBar()` / `ImGui::BeginMenu("Layout")` with items `"Save Layout…"`, `"Load Layout…"`, `"Reset to Default"`; clicking each sets a boolean flag to open the corresponding modal dialog (depends on T027)
- [ ] T030 [US3] Implement **Save Layout** modal dialog in `src/editor/src/Editor.cpp`: `ImGui::InputText` for the layout name; "Save" button calls `m_layoutManager->saveLayout(name)`; if name already exists in `listLayouts()`, show "Overwrite?" confirmation step before saving; on success close modal (depends on T029)
- [ ] T031 [US3] Implement **Load Layout** modal dialog in `src/editor/src/Editor.cpp`: `ImGui::ListBox` (or `ImGui::Selectable` in a child window) listing all `m_layoutManager->listLayouts()` entries; "Load" button calls `m_layoutManager->loadLayout(selected)`; on success close modal (depends on T029)
- [ ] T032 [US3] Wire `LayoutManager` into `Editor`: pass `layoutsDir` from `EditorFactory`; call `m_layoutManager->restoreLastLayout()` at end of `Editor::init()`; call `m_layoutManager->resetToDefault()` when "Reset to Default" is selected (depends on T027, T029–T031)

**Checkpoint**: Full save/load/restore cycle works across engine restarts. `ctest --output-on-failure` passes.

---

## Phase 6: User Story 4 — Engine Log Viewing (Priority: P4)

**Goal**: Log panel shows timestamped, severity-coloured engine messages; auto-scrolls to latest; pauses on user scroll-up; resumes on scroll-to-bottom; filter by severity; clear button.

**Independent Test**: Launch engine → startup messages appear in Log panel within 1 second with timestamps and severity labels; scroll up → auto-scroll pauses; scroll to bottom → auto-scroll resumes; click "Errors" filter → only error-level messages shown; click "Clear" → panel empties.

- [ ] T033 [US4] Create `src/editor/src/panels/LogPanel.hpp` and `src/editor/src/panels/LogPanel.cpp` implementing `IPanel`: holds a `LogBuffer` (shared with `EditorLogSink`); `draw()` renders: (a) three toggle buttons for Info/Warning/Error filter; (b) "Clear" button that calls `buffer.clear()`; (c) `ImGui::BeginChild("##log")` scrollable region iterating filtered entries — Info in default colour, Warning in yellow, Error in red via `ImGui::PushStyleColor`; auto-scroll logic: track `m_autoScroll` bool, set `false` if `ImGui::GetScrollY() < ImGui::GetScrollMaxY()`, set `true` and call `ImGui::SetScrollHereY(1.0f)` when user scrolls to bottom or `m_autoScroll` is `true`
- [ ] T034 [US4] Update `src/editor/src/log/EditorLogSink.hpp` and `src/editor/src/log/EditorLogSink.cpp`: replace internal deque with a `std::shared_ptr<LogBuffer>` passed at construction; `sink_it_()` locks the mutex, maps spdlog level to `LogSeverity`, constructs `LogEntry{timestamp, severity, formatted_message}`, appends to buffer, drops oldest if size > 10,000 (depends on T033)
- [ ] T035 [US4] Update `src/editor/src/Editor.cpp`: construct `EditorLogSink` with the shared `LogBuffer` from `LogPanel`; register sink via `spdlog::default_logger()->sinks().push_back(sink)` during `Editor::init()`; add `LogPanel` to the panel list so it renders in the dockspace (depends on T033, T034)
- [ ] T036 [P] [US4] Write `tests/unit/editor/LogPanelTest.cpp`: construct `LogPanel` directly; push 10,001 entries to its `LogBuffer`; assert buffer size stays ≤ 10,000 (oldest dropped); assert `LogFilter` with `showErrors = false` excludes error entries from iteration; assert `m_autoScroll` starts `true`, is set `false` when simulated scroll position < max, and is restored `true` when scrolled to bottom; add to `tests/unit/editor/CMakeLists.txt`

**Checkpoint**: Log panel shows engine startup messages on launch. All severity/filter/scroll/clear behaviors work. `ctest --output-on-failure` passes.

---

## Phase 7: Polish & Cross-Cutting Concerns

- [ ] T037 Update `README.md` to note that ImGui is fetched automatically, no new user-installed prerequisites; update build/run instructions to reflect editor window launch
- [ ] T038 [P] Validate `quickstart.md` build steps against actual build output; correct any path or command discrepancies
- [ ] T039 [P] Run `ctest --output-on-failure` across all targets (`SonnetLoggerTests`, `SonnetWindowTests`, `SonnetRendererTests`, `SonnetEditorTests`, `SonnetIntegrationTests`) and confirm zero failures
- [ ] T040 Verify all Complexity Tracking entries in `specs/005-editor-ui-layout/plan.md` match the hardcoded strings actually present in the merged source (panel titles, menu items)

**Checkpoint**: All tests pass. README and quickstart reflect current state. Plan Complexity Tracking is accurate.

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies — start immediately
- **Phase 2 (Foundational)**: Depends on Phase 1 completion — BLOCKS all user stories
- **Phase 3 (US1)**: Depends on Phase 2 — no other story dependencies
- **Phase 4 (US2)**: Depends on Phase 3 (requires Editor and panels to exist)
- **Phase 5 (US3)**: Depends on Phase 3 (requires Editor and ImGui context)
- **Phase 6 (US4)**: Depends on Phase 3 (requires Editor init and panel system); independent of US2/US3
- **Phase 7 (Polish)**: Depends on all desired user stories being complete

### User Story Dependencies

- **US1 (P1)**: After Foundational — no story dependencies
- **US2 (P2)**: Requires US1 complete (configures the Editor and panels built in US1)
- **US3 (P3)**: Requires US1 complete (adds menu bar and LayoutManager to the Editor)
- **US4 (P4)**: Requires US1 complete (adds LogPanel to the Editor); independent of US2 and US3

### Within Each Phase — Execution Order

```
Phase 3:  T014 ──┐
          T015 ──┤
          T016 ──┼──→ T018 → T019 → T020 → T021
          T017 ──┘              ↘ T022 [P]
                                  T023 [P]

Phase 5:  T027 → T029 → T030 → T031 → T032
          T028 [P] (parallel with T027+)

Phase 6:  T033 → T034 → T035
          T036 [P] (parallel with T033+)
```

---

## Parallel Execution Examples

### Phase 1
```
[parallel] T001, T002, T003, T004, T006
[serial]   T005 (after T004)
```

### Phase 2
```
[parallel] T007, T009, T010, T011, T012
[serial]   T008 (after T007), T013 (after T009–T012)
```

### Phase 3 (US1)
```
[parallel] T014, T015, T016, T017
[serial]   T018 (after T014)
           T019 (after T015, T016, T017, T018)
           T020 (after T019)
           T021 (after T020)
[parallel] T022, T023 (after T019/T020)
```

### Phase 5 (US3)
```
[parallel] T027, T028
[serial]   T029 (after T027)
           T030, T031 (after T029, can be parallel with each other)
           T032 (after T030 + T031)
```

---

## Implementation Strategy

### MVP (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational
3. Complete Phase 3: User Story 1
4. **STOP and VALIDATE**: Run engine, confirm four-panel layout and triangle viewport
5. Demo if ready

### Incremental Delivery

1. Phase 1 + 2 → Build infrastructure ready
2. Phase 3 (US1) → Familiar editor layout with viewport (**MVP**)
3. Phase 4 (US2) → Panel rearrangement
4. Phase 5 (US3) + Phase 6 (US4) → These are **independent** and can proceed in parallel:
   - One track: Layout save/load (US3)
   - Another track: Log panel (US4)
5. Phase 7 → Polish and validation

---

## Notes

- `[P]` tasks touch different files and have no unresolved dependencies — safe to parallelize
- `[USn]` label maps each task to its user story for independent traceability
- US2, US3, US4 each build on the Editor constructed in US1 — **do not skip Phase 3**
- ImGui docking handles drag/resize/tab/float natively — US2 tasks are configuration, not re-implementation
- All tests run headlessly (`ctest --output-on-failure`) — no display or GPU required
- Layout `.ini` files and `active.txt` live adjacent to the executable; clean builds reset them
