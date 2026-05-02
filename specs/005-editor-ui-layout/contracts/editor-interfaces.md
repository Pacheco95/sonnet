# Interface Contracts: Editor UI Layout

**Branch**: `005-editor-ui-layout` | **Date**: 2026-05-02

These are the C++ public interface contracts for the `SonnetEditor` module. All types listed here appear in `src/editor/include/sonnet/editor/`. No ImGui, Vulkan, SDL3, or spdlog types appear in these public headers.

---

## IEditor

**File**: `include/sonnet/editor/IEditor.hpp`  
**Purpose**: Lifecycle and frame orchestration for the editor UI system.

```cpp
namespace sonnet::editor {

class IEditor {
public:
    virtual ~IEditor() = default;

    // Initialise editor: sets up ImGui context, registers log sink,
    // loads active layout. Returns false on failure.
    virtual bool init(sonnet::window::IWindow& window,
                      sonnet::renderer::IRendererBackend& backend) = 0;

    virtual void shutdown() = 0;

    // Process a platform event before ImGui; returns true if ImGui consumed it.
    virtual bool processEvent(void* sdlEvent) = 0;

    // Called once per frame: runs all panel UIs and renders ImGui draw data.
    virtual void render() = 0;
};

} // namespace sonnet::editor
```

**Invariants**:
- `render()` MUST NOT be called before `init()` returns `true`.
- `processEvent()` MUST be called for every SDL event before `render()` in the same frame.
- `shutdown()` is idempotent.

---

## IPanel

**File**: `include/sonnet/editor/IPanel.hpp`  
**Purpose**: Per-panel rendering contract.

```cpp
namespace sonnet::editor {

class IPanel {
public:
    virtual ~IPanel() = default;

    // Returns the human-readable panel title shown in the ImGui title bar / tab.
    [[nodiscard]] virtual const char* title() const = 0;

    // Returns false if this panel may be closed by the user (default: true = closable).
    [[nodiscard]] virtual bool isPermanent() const { return false; }

    // Draw this panel's ImGui content. Called between ImGui::Begin/End by the editor.
    virtual void draw() = 0;
};

} // namespace sonnet::editor
```

**Notes**:
- The Viewport panel overrides `isPermanent()` to return `true` (FR-007: cannot be closed).
- Placeholder panels (SceneHierarchy, Inspector) implement `draw()` to show a stub label.

---

## ILayoutManager

**File**: `include/sonnet/editor/ILayoutManager.hpp`  
**Purpose**: Save, load, and enumerate named editor layouts.

```cpp
namespace sonnet::editor {

class ILayoutManager {
public:
    virtual ~ILayoutManager() = default;

    // Returns all available saved layout names (alphabetical order).
    [[nodiscard]] virtual std::vector<std::string> listLayouts() const = 0;

    // Saves current ImGui ini state under `name`. Overwrites if it already exists.
    // Returns false if the name is invalid or write fails.
    virtual bool saveLayout(std::string_view name) = 0;

    // Loads the layout named `name` into ImGui state.
    // Returns false if not found or file is unreadable/corrupt (falls back to default).
    virtual bool loadLayout(std::string_view name) = 0;

    // Returns the name of the most recently active layout, or empty string if none.
    [[nodiscard]] virtual std::string activeLayoutName() const = 0;

    // Restores the last active layout on startup; falls back to default if unavailable.
    virtual void restoreLastLayout() = 0;

    // Resets ImGui layout state to the built-in default without deleting saved layouts.
    virtual void resetToDefault() = 0;
};

} // namespace sonnet::editor
```

**Error handling**:
- `loadLayout()` failure MUST emit a `SONNET_LOG_WARN` or `SONNET_LOG_ERROR` line before falling back.
- `saveLayout()` failure MUST emit a `SONNET_LOG_ERROR` line.

---

## EditorFactory

**File**: `include/sonnet/editor/EditorFactory.hpp`  
**Purpose**: Creates the concrete `ImGuiEditor` instance without exposing it.

```cpp
namespace sonnet::editor {

// Returns the default ImGui-based editor implementation.
std::unique_ptr<IEditor> createEditor(const std::filesystem::path& layoutsDir);

} // namespace sonnet::editor
```

**Notes**:
- `layoutsDir` is the directory where `layouts/*.ini` and `layouts/active.txt` are stored.
- Typically: adjacent to the engine executable or in a user preferences directory.

---

## RendererNativeHandles (in SonnetRenderer)

**File**: `include/sonnet/renderer/IRendererBackend.hpp` (addition to existing header)  
**Purpose**: Opaque handle bundle for editor UI backend initialization.

```cpp
namespace sonnet::renderer {

struct RendererNativeHandles {
    uint64_t instance{0};
    uint64_t physicalDevice{0};
    uint64_t device{0};
    uint64_t graphicsQueue{0};
    uint64_t renderPass{0};
    uint64_t descriptorPool{0};  // pool dedicated for ImGui descriptors
    uint32_t graphicsQueueFamily{0};
    uint32_t imageCount{0};
};

} // namespace sonnet::renderer
```

**Addition to `IRendererBackend`**:
```cpp
// Returns native GPU handles for editor UI initialization.
// Returns a zero-initialized struct if the backend does not support editor integration.
[[nodiscard]] virtual RendererNativeHandles getNativeHandles() const { return {}; }
```
