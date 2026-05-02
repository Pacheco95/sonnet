# Data Model: Editor UI Layout

**Branch**: `005-editor-ui-layout` | **Date**: 2026-05-02

## Entities

### PanelType (enum)

The set of known panel types in this feature release.

| Value | Description |
|---|---|
| `Viewport` | 3D scene viewport; renders the scene to an offscreen texture |
| `Log` | Engine log console; displays `LogEntry` items |
| `SceneHierarchy` | Placeholder — displays panel title and stub content |
| `Inspector` | Placeholder — displays panel title and stub content |

---

### PanelState

Runtime state of a single panel instance.

| Field | Type | Description |
|---|---|---|
| `type` | `PanelType` | Which panel this is |
| `visible` | `bool` | Whether the panel is currently shown |

> Positioning, sizing, dock state, and tab-group membership are owned entirely by ImGui's docking state machine and serialized automatically via `ImGui::SaveIniSettingsToMemory()`. They are not stored separately.

---

### LogEntry

A single captured engine log message.

| Field | Type | Constraints |
|---|---|---|
| `timestamp` | `std::chrono::system_clock::time_point` | Set at capture time by `EditorLogSink` |
| `severity` | `LogSeverity` | `Info`, `Warning`, or `Error` |
| `message` | `std::string` | Full formatted message text including source file and line |

**`LogSeverity` mapping from spdlog levels**:

| spdlog level | LogSeverity |
|---|---|
| `trace`, `debug`, `info` | `Info` |
| `warn` | `Warning` |
| `error`, `critical` | `Error` |

---

### LogBuffer

In-memory store for `LogEntry` items, owned by the log panel.

| Field | Type | Constraints |
|---|---|---|
| `entries` | `std::deque<LogEntry>` | Ring-capped at 10,000 entries; oldest dropped on overflow |
| `mutex` | `std::mutex` | Guards `entries` for cross-thread access (spdlog sink vs. render thread) |

---

### LogFilter

Active display filter for the log panel.

| Field | Type | Default | Description |
|---|---|---|---|
| `showInfo` | `bool` | `true` | Show `Info`-severity entries |
| `showWarnings` | `bool` | `true` | Show `Warning`-severity entries |
| `showErrors` | `bool` | `true` | Show `Error`-severity entries |

---

### NamedLayout

A persisted named editor layout.

| Field | Storage | Description |
|---|---|---|
| `name` | Filename stem (`layouts/<name>.ini`) | User-supplied name; must be a valid filename |
| `imguiIniContent` | File body of `layouts/<name>.ini` | Raw output of `ImGui::SaveIniSettingsToMemory()` |
| `createdAt` | N/A (filesystem mtime at first write) | Not stored explicitly |
| `modifiedAt` | N/A (filesystem mtime on overwrite) | Not stored explicitly |

**Uniqueness**: Layout names are unique per the `layouts/` directory. Name comparison is case-insensitive on Windows (filesystem-native), case-sensitive on Linux. The editor presents names as-typed.

**Active layout tracking**: The file `layouts/active.txt` contains exactly one line: the name of the most recently activated layout. If absent or blank, the built-in default layout is used.

---

### RendererNativeHandles

Opaque struct passed from `IRendererBackend` to `SonnetEditor` for ImGui Vulkan init. All fields are `uint64_t` — no Vulkan types appear in `SonnetRenderer`'s public headers.

| Field | Type | Vulkan equivalent |
|---|---|---|
| `instance` | `uint64_t` | `VkInstance` |
| `physicalDevice` | `uint64_t` | `VkPhysicalDevice` |
| `device` | `uint64_t` | `VkDevice` |
| `graphicsQueue` | `uint64_t` | `VkQueue` |
| `renderPass` | `uint64_t` | `VkRenderPass` (swapchain render pass) |
| `graphicsQueueFamily` | `uint32_t` | Graphics queue family index |
| `imageCount` | `uint32_t` | Number of swapchain images |
| `descriptorPool` | `uint64_t` | `VkDescriptorPool` dedicated for ImGui |

---

## Relationships

```
EditorState
  ├── panels[4]           : PanelState (one per PanelType)
  ├── logBuffer           : LogBuffer
  └── logFilter           : LogFilter

EditorLogSink → LogBuffer  (pushes LogEntry items)

LayoutManager
  └── reads/writes NamedLayout files (layouts/<name>.ini, layouts/active.txt)

IRendererBackend::getNativeHandles() → RendererNativeHandles
  └── consumed by ImGuiEditor during init to configure ImGui Vulkan backend
```

## State Transitions

### Panel dock state (owned by ImGui)

```
docked ──drag-out──→ floating
floating ──drag-onto-panel──→ docked
floating ──drag-onto-tab-bar──→ tabbed
tabbed ──drag-tab-out──→ floating
```

### Log panel auto-scroll state

```
auto-scrolling ──user scrolls up──→ paused
paused ──user scrolls to bottom──→ auto-scrolling
```

### Layout lifecycle

```
[no layouts] ──save──→ named layout exists
named layout ──load──→ active in session
active ──save (same name, confirm)──→ overwritten
named layout ──engine restart──→ restored from active.txt
[active.txt missing] ──engine start──→ default layout loaded
[corrupt .ini] ──load attempt──→ fallback to default + log warning
```

## Validation Rules

- Layout name: non-empty; must not contain path separators (`/`, `\`); stripped of leading/trailing whitespace before save.
- Log entries: message field is never empty (guaranteed by spdlog before reaching the sink).
- Ring buffer overflow: oldest entry silently dropped; no error raised.
- Viewport panel: `visible` is always `true`; the panel cannot be closed (FR-007).
