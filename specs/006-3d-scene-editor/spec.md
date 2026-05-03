# Feature Specification: 3D Scene Editor

**Feature Branch**: `006-3d-scene-editor`  
**Created**: 2026-05-03  
**Status**: Draft  
**Input**: User description: "As a game developer using sonnet. I want to be able to instantiate 3D primitives in the rendering viewport..."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Primitive Instantiation and Lighting (Priority: P1)

A game developer opens the editor and adds 3D primitive objects (cube, sphere, cylinder, plane, capsule) to the scene. A directional light illuminates the scene so objects are visible with shading. The developer can add at most one directional light; attempting to add a second one is prevented.

**Why this priority**: Without objects and basic lighting, no other feature can be tested or used. This is the foundational capability the entire editor depends on.

**Independent Test**: Can be tested by spawning primitives and confirming they appear rendered in the viewport with lighting. Delivers a visible 3D scene even without any interaction.

**Acceptance Scenarios**:

1. **Given** the editor is open with an empty scene, **When** the developer selects a primitive type (e.g., Cube) from the menu bar "Add" menu, **Then** a cube appears at a default position in the viewport, rendered with directional lighting.
2. **Given** no directional light exists in the scene, **When** the developer adds a directional light, **Then** it appears in the viewport and illuminates all objects.
3. **Given** a directional light already exists, **When** the developer opens the Add menu, **Then** the "Directional Light" entry is grayed out and hovering it shows a tooltip explaining that only one directional light is allowed.
4. **Given** the scene has multiple primitives, **When** the developer views the viewport, **Then** all primitives are lit by the directional light and visually distinguishable by shape and shading.

---

### User Story 2 - Object Selection and Gizmo Manipulation (Priority: P2)

A game developer clicks on an object in the viewport to select it. A visual outline highlights the selected object and 3D gizmos appear to manipulate its position, rotation, or scale. The developer can toggle between gizmo modes using keyboard shortcuts (W for translate, E for rotate, R for scale). Clicking in empty space deselects the object, hiding the outline and gizmos.

**Why this priority**: Manipulating objects is the primary editorial action. Without selection and gizmos, the developer cannot arrange the scene.

**Independent Test**: Can be tested by placing a primitive, clicking it, and verifying the outline and gizmos appear. Delivers the core scene-editing workflow.

**Acceptance Scenarios**:

1. **Given** a primitive is in the scene, **When** the developer clicks it in the viewport, **Then** a selection outline appears around the object and position gizmos are shown by default.
2. **Given** an object is selected, **When** the developer presses W, **Then** translate gizmos are active; pressing E activates rotate gizmos; pressing R activates scale gizmos.
3. **Given** an object is selected with translate gizmos active, **When** the developer drags a gizmo axis, **Then** the object moves along that axis in the scene.
4. **Given** an object is selected with rotate gizmos active, **When** the developer drags a rotation ring, **Then** the object rotates around that axis.
5. **Given** an object is selected with scale gizmos active, **When** the developer drags a scale handle, **Then** the object scales along that axis (or uniformly for the center handle).
6. **Given** an object is selected, **When** the developer clicks on empty space in the viewport, **Then** the selection is cleared, the outline disappears, and the gizmos are hidden.

---

### User Story 3 - Fly Camera Navigation (Priority: P3)

A game developer navigates the 3D scene using a fly camera. While holding the right mouse button, the developer uses WASD keys to move forward/backward/left/right and Q/E (or similar) to move up/down; mouse movement controls look direction. Releasing the right mouse button returns to normal cursor mode.

**Why this priority**: A fly camera is essential for moving through and inspecting a 3D scene from any angle.

**Independent Test**: Can be tested independently by flying around an empty scene. Delivers full spatial navigation capability.

**Acceptance Scenarios**:

1. **Given** the viewport is focused, **When** the developer holds the right mouse button, **Then** the cursor is hidden and fly-camera mode is activated.
2. **Given** fly-camera mode is active, **When** the developer moves the mouse, **Then** the camera rotates to look in the corresponding direction smoothly.
3. **Given** fly-camera mode is active, **When** the developer presses W/A/S/D, **Then** the camera moves forward/left/backward/right relative to the camera's facing direction.
4. **Given** fly-camera mode is active, **When** the developer releases the right mouse button, **Then** the camera stops and normal cursor interaction resumes.

---

### User Story 4 - Scene Hierarchy Visibility and Parenting (Priority: P4)

All scene objects — including primitives and the directional light — appear in a scene hierarchy panel. The developer can select an object by clicking its entry in the hierarchy, and can drag objects onto other objects to establish parent/child relationships. Unparenting an object preserves its world-space position, rotation, and scale.

**Why this priority**: The hierarchy is how developers organize complex scenes. Parenting enables grouped transformations without losing spatial context.

**Independent Test**: Can be tested by adding multiple objects and verifying they appear in the hierarchy list. Parenting can be verified by reparenting and checking that the world transform is preserved.

**Acceptance Scenarios**:

1. **Given** objects exist in the scene, **When** the developer views the hierarchy panel, **Then** all primitives and the directional light are listed by name with indentation reflecting parent/child relationships.
2. **Given** the hierarchy panel is visible, **When** the developer clicks an object's name in the hierarchy, **Then** that object becomes selected in the viewport (showing outline and gizmos).
3. **Given** two objects A and B in the hierarchy, **When** the developer drags object B onto object A, **Then** B becomes a child of A, and B's visual appearance in the world does not change (world transform preserved).
4. **Given** object B is a child of object A, **When** the developer moves A with gizmos, **Then** B moves with A, maintaining its relative position.
5. **Given** object B is a child of object A, **When** the developer unparents B (by dragging it to the root level or by right-clicking it and selecting "Unparent"), **Then** B retains its current world-space position, rotation, and scale.

---

### User Story 5 - Inspector Panel Editing (Priority: P5)

A game developer selects an object and uses the inspector panel to read and manually edit its position, rotation, and scale as numeric values. Changes entered in the inspector are immediately reflected in the viewport.

**Why this priority**: Precise numeric input complements gizmo manipulation for exact positioning.

**Independent Test**: Can be tested by selecting an object, typing a value in the inspector position field, and confirming the object moves to that position in the viewport.

**Acceptance Scenarios**:

1. **Given** an object is selected, **When** the developer views the inspector panel, **Then** the current position (X, Y, Z), rotation (X, Y, Z in degrees), and scale (X, Y, Z) are displayed as editable numeric fields.
2. **Given** the inspector is showing an object's transform, **When** the developer changes the X position value and presses Enter (or Tab to advance), **Then** the object moves to the new X position in the viewport immediately.
3. **Given** the inspector is showing an object's rotation, **When** the developer enters a new rotation value and presses Enter, **Then** the object rotates to that angle in the viewport.
4. **Given** no object is selected, **When** the developer views the inspector panel, **Then** the panel shows no transform fields (empty state).

---

### User Story 6 - Directional Light Configuration (Priority: P6)

A game developer selects the directional light in the viewport or hierarchy and uses the inspector panel or rotation gizmos to adjust its color, intensity, and direction. Changes are reflected immediately in the scene lighting.

**Why this priority**: Lighting quality directly affects visual fidelity. Configuring the light is important but depends on the light and selection systems being in place first.

**Independent Test**: Can be tested by selecting the directional light, changing its color to red in the inspector, and confirming all lit objects take on a red tint.

**Acceptance Scenarios**:

1. **Given** a directional light exists, **When** the developer selects it in the viewport or hierarchy, **Then** the inspector shows fields for color (color picker), intensity (numeric), and the rotation gizmos appear for direction control.
2. **Given** the directional light is selected, **When** the developer changes the color in the inspector, **Then** all objects in the scene are immediately illuminated with the new color.
3. **Given** the directional light is selected, **When** the developer increases or decreases the intensity value, **Then** the scene brightness changes accordingly in real time.
4. **Given** the directional light is selected, **When** the developer drags the rotation gizmo, **Then** the light direction changes and shadows/shading update accordingly in the viewport.

---

### Edge Cases

- What happens when trying to add a second directional light? The "Directional Light" entry in the Add menu is grayed out when one already exists; hovering it shows a tooltip explaining the one-light limit. The existing light remains unchanged.
- What happens when parenting an object that is already a parent of another object? The full sub-hierarchy moves with it; no orphaned children.
- What happens when the developer attempts to create a circular parent relationship (e.g., dragging a parent onto its own descendant)? The drag is silently ignored; the hierarchy remains unchanged.
- What happens when the scene has zero objects and the developer presses W/E/R? The keyboard shortcut has no effect (no object selected).
- What happens when the developer clicks an empty area of the hierarchy panel (not on any item)? The selection is cleared, matching the viewport behavior.
- What happens to a child object's world position when the parent is deleted? (Out of scope for this feature — deletion is not specified and should not be implemented here.)

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The system MUST allow the developer to add the following 3D primitive types to the scene via a menu bar "Add" menu: Cube, Sphere, Cylinder, Plane, and Capsule.
- **FR-002**: The system MUST render all scene objects with shading influenced by the directional light direction, color, and intensity.
- **FR-003**: The system MUST prevent more than one directional light from existing in the scene simultaneously.
- **FR-004**: The system MUST allow the developer to activate fly-camera mode by holding the right mouse button, and navigate using WASD keys and mouse look.
- **FR-005**: The developer MUST be able to select a single scene object by clicking on it in the viewport. For the directional light, clicking its editor-only billboard icon selects it.
- **FR-006**: The developer MUST be able to select a scene object by clicking its name in the scene hierarchy panel.
- **FR-007**: When an object is selected, the system MUST display a visible selection outline around the object.
- **FR-008**: When an object is selected, the system MUST display 3D manipulation gizmos (translate, rotate, or scale) in the viewport at the object's position. All gizmos operate in world-space; no local-space toggle is required.
- **FR-009**: The developer MUST be able to switch between translate, rotate, and scale gizmos using keyboard shortcuts (W, E, R respectively) while an object is selected.
- **FR-010**: The developer MUST be able to drag gizmo handles to move, rotate, or scale the selected object.
- **FR-011**: Clicking in empty space in the viewport MUST deselect the current object, hide the selection outline, and hide the gizmos.
- **FR-012**: All scene objects — primitives and the directional light — MUST appear in the scene hierarchy panel, reflecting parent/child relationships through indentation.
- **FR-013**: The developer MUST be able to parent one object to another by dragging it onto the target parent in the hierarchy; the child's world-space transform MUST be preserved.
- **FR-014**: The developer MUST be able to unparent an object from its parent via either: (a) dragging it to the root level in the hierarchy, or (b) right-clicking it in the hierarchy and selecting "Unparent" from the context menu. The object's world-space transform MUST be preserved in both cases.
- **FR-015**: The inspector panel MUST display and allow editing of the selected object's position (X, Y, Z), rotation (X, Y, Z in degrees), and scale (X, Y, Z) as numeric input fields.
- **FR-016**: Inspector panel transform fields MUST apply their value to the viewport when the developer presses Enter (confirm current field) or Tab (confirm and advance to next field).
- **FR-017**: When the directional light is selected, the inspector MUST expose controls for its color (color picker), intensity (numeric), and its rotation is editable via the rotation gizmo or inspector fields.
- **FR-018**: Changes to the directional light's color, intensity, and direction MUST immediately update the scene lighting in the viewport.

### Key Entities

- **Scene Object**: A node in the scene with an auto-generated name (type name + incrementing number, e.g., "Cube", "Cube1"), a 3D transform (position, rotation, scale), and a visual representation (primitive mesh or light). Can have zero or one parent and zero or more children.
- **Primitive Mesh**: A built-in geometric shape (Cube, Sphere, Cylinder, Plane, Capsule) that can be rendered with lighting.
- **Directional Light**: A special scene object with color, intensity, and direction attributes. Exactly zero or one may exist per scene. Represented in the editor viewport by a clickable 3D billboard icon (sun/light symbol) at a fixed world position, always facing the camera; this icon is editor-only and not rendered in the final scene.
- **Transform**: The combination of position (3D vector), rotation (Euler angles in degrees), and scale (3D vector) that defines an object's placement in the world.
- **Selection**: The currently active scene object; at most one object can be selected at a time.
- **Scene Hierarchy**: A tree structure representing all scene objects and their parent/child relationships.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A developer can add a primitive to the scene and see it rendered with lighting within 1 second of selecting the primitive type.
- **SC-002**: Selecting an object by clicking it in the viewport or hierarchy takes under 100 milliseconds to show the outline and gizmos.
- **SC-003**: Gizmo manipulation (drag to move/rotate/scale) updates the object's transform continuously without perceptible lag.
- **SC-004**: Switching between translate, rotate, and scale gizmo modes via keyboard shortcut (W, E, R) takes effect immediately (under 50 milliseconds).
- **SC-005**: Inspector panel numeric edits are reflected in the viewport within one frame of confirmation.
- **SC-006**: Parenting and unparenting an object preserves its world-space position, rotation, and scale with no visible pop or drift.
- **SC-007**: Directional light color and intensity changes are reflected in the scene within one frame, with no perceptible delay.
- **SC-008**: 100% of developers can complete a primary scene-building workflow (add primitive → select → move with gizmo → parent to another object) without consulting documentation.

## Clarifications

### Session 2026-05-03

- Q: How are primitives and the directional light added to the scene? → A: Via a top-level "Add" menu in the application menu bar, which contains a dropdown listing all primitive types (Cube, Sphere, Cylinder, Plane, Capsule) and Directional Light.
- Q: Should gizmos operate in world-space or local-space, and can the user toggle between them? → A: World-space only; no toggle required.
- Q: How are newly added objects automatically named in the hierarchy? → A: Type name + incrementing number (first instance uses type name alone: "Cube"; subsequent ones append a number: "Cube1", "Cube2"; directional light is named "DirectionalLight").
- Q: What feedback is shown when the user tries to add a second directional light? → A: The "Directional Light" entry in the Add menu is grayed out (disabled) when one already exists, and a tooltip explains the one-light limit when the entry is hovered.
- Q: How does the developer unparent an object in the scene hierarchy? → A: Both mechanisms are supported: dragging the object to the root level of the hierarchy, and right-clicking the object in the hierarchy and selecting "Unparent" from the context menu.

### Session 2026-05-03 (continued)

- Q: How is the directional light represented in the viewport so the developer can click to select it? → A: A 3D billboard icon (sun/light symbol) placed at a fixed world position, always facing the camera, clickable for selection, visible in the editor viewport only (not rendered in-game).
- Q: How does the developer confirm a value entered in the inspector panel? → A: Pressing Enter confirms the value and updates the viewport; pressing Tab confirms and moves focus to the next field.
- Q: What happens when the developer tries to parent an object to one of its own descendants (circular parenting)? → A: The drag is silently ignored; the hierarchy remains unchanged with no error dialog.

## Assumptions

- Single-object selection only; multi-select is out of scope for this feature.
- Fly-camera mode is activated by holding the right mouse button; releasing it returns to normal cursor/interaction mode.
- Default gizmo mode when selecting an object is translate (position).
- Rotation values in the inspector are displayed as Euler angles in degrees (X, Y, Z order).
- Primitives are added at the world origin (position 0, 0, 0) with default rotation and unit scale.
- The directional light is added with a default white color, moderate intensity, and a downward-angled direction.
- No scene persistence (load/save) is required for this feature.
- Object deletion is out of scope for this feature.
- Undo/redo is out of scope for this feature.
- The application already has a rendering viewport and inspector/hierarchy panels from the existing editor UI layout.
