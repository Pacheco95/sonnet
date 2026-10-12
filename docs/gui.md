# gui

The game UI: menus, a heads-up display, a pause screen as entities in the scene, with components that `gui` registers, lays out and hit-tests ([ADR-0026](decisions/0026-game-ui.md)). Depends on `world` publicly, so `core`, `platform`, `rhi`, `renderer` and `assets` come with it. `scripting` links it to read its events, as it links `physics`. It is not `ui`, which stays Dear ImGui for the editor and which the player never links.

This is M15 in parts: the components, layout, input and events; the drawing of images and rectangles; the editor's view of a canvas and the sample menu; and fonts and text, which are still to come, so text is not drawn yet.

| Header | Contents |
|---|---|
| `Components.h` | `GuiCanvas`, `GuiRect`, `GuiImage`, `GuiText`, `GuiButton`, `GuiLayout`, their enums, and `registerComponents` |
| `Layout.h` | `layoutGui`: the rectangle of every element, in draw order; `GuiElement`, `PixelRect`, `findElement` |
| `GuiSystem.h` | `GuiSystem`: the layout of a frame, the events offered to it before the game, the `GuiEvent`s it records, and the quads and the pass that draw it |

## Components

The components are plain structs registered with the world's reflection ([world.md](world.md#components)), so the inspector edits them, scenes save them (the scene stays at version 3: components are added, none changed) and scripts read and write them like the core ones. `GuiSystem` registers them; `registerComponents` does it alone, for tools that load scenes without running a UI (the editor), and does nothing the second time. Every length is in reference pixels, which the canvas scales to the target.

- `GuiCanvas` is the root of a tree: `referenceSize` (1920 × 1080), `match`, `safeArea` and `order`.
- `GuiRect` is an element's place inside its parent's rectangle: `anchorMin` and `anchorMax` (0 to 1 over the parent), `pivot`, `offsetMin` and `offsetMax`. The element runs from the parent's point at `anchorMin` plus `offsetMin` to its point at `anchorMax` plus `offsetMax`: a fixed size at a corner, a stretched panel and a centred button are the same fields. An element's parent is its parent entity, which holds a `GuiRect` or a `GuiCanvas`; the `Transform` every entity has is not read. An entity without a `GuiRect` under the tree is not an element, and nothing under it is either.
- `GuiImage` has a `texture` (an identity), a `tint` (sRGB, with alpha) and `slice`, the nine-slice borders in texture pixels. `blocksInput` makes it take pointer events without a button.
- `GuiText` has a `font` (an identity), `text`, `size`, `color`, `horizontal` and `vertical` alignment and `wrap`. The font asset type does not exist yet, so nothing resolves `font`.
- `GuiButton` has `interactable` and three tints, normal, pressed and disabled, for the `GuiImage` on the same entity. It carries no script: the `Scripts` on the same entity hear its events.
- `GuiLayout` on an element sizes and places its children in a row or a column, in sibling order.

An entity with the `Disabled` tag is hidden and takes no input, and so is everything under it: the layout and the hit test apply the tag to the subtree themselves, which the tag alone does not do for the children.

## Layout

`layoutGui(world, targetSize, safeArea)` is a plain function from the entity tree and the target's size to a rectangle per element, in target pixels, run once a frame by `GuiSystem::update` after the world has updated. Nothing is cached. The result is in draw order: canvases by `order` and then entity, and under each canvas its tree depth first in the order of `World::children`, so what comes later is in front.

- **Scale.** The target against the canvas's `referenceSize`, by width at `match` 0 and by height at 1; in between the two logarithms are blended, so a target twice as wide and 1.5 times as tall scales by √3 at 0.5. A canvas scales from the whole target, not from its safe area, so a notch does not change the size of text.
- **Safe area.** A canvas with `safeArea` takes the part of the target inside the platform's ([platform.md](platform.md#window)); an empty one is no inset.
- **Anchors.** As `GuiRect` says, with the offsets multiplied by the scale. `pivot` is reported in the result, in target pixels, and moves nothing yet: the corners define the rectangle, and there is no rotation or scale for a pivot to turn about.
- **Layout.** Under a parent with a `GuiLayout`, the children's anchors are ignored and each takes its size, `offsetMax - offsetMin`. They run along the axis from the padding (`left, top, right, bottom`) with `spacing` between; `alignment` puts the block along the axis and each child across it at the start, the centre or the end of the padding's inside. Hidden children take no room.

## Input

`GuiSystem::event` is offered each `platform::Event` before the game, and says whether it consumed it. A press (a mouse button or a finger) that lands on an interactable `GuiButton` or on a `GuiImage` with `blocksInput`, front to back over the elements, is consumed and its pointer is held by that element until it is released; so are the motion and the release of a held pointer. Everything else is the game's. A label or a panel without those is not in the way. `runtime::Game::event` calls it first, so a consumed pointer never reaches `InputState` or `input.touches()`.

Positions are window coordinates and are mapped to the target by the ratio of the sizes given to `update`. SDL's mouse synthesised from a finger arrives as well as the finger ([platform.md](platform.md#events)); both land on the same button, which counts them as one press until the last is released. Only the left mouse button and fingers make events; another mouse button over a button is consumed and says nothing. Losing the window's focus lets go of every held pointer, without a click.

## Events

`GuiSystem` records a `GuiEvent` (kind, entity, pointer) for a button: `Press` when a pointer goes down on it, `Release` when the last one holding it goes up, and `Click` after that release when it was over the button. `scripting` delivers them at the start of each `update` frame as `onPress(self)`, `onRelease(self)` and `onClick(self)` on the instances of the button's entity, in slot order ([scripting.md](scripting.md#events)). The application calls `endFrame` after the world's frame; `Game` does.

## Drawing

`GuiSystem::addPass(renderer, assets, graph, target)` builds the frame's quads and declares the renderer's overlay pass ([rendering.md](rendering.md#frame-structure)) over `target`. `runtime::Game::render` calls it after the scene passes and before the screenshot, into the same target the player copies to the swapchain and the capture reads, so a capture shows what the player sees. A game with no canvas, or none that draws, declares no pass and renders byte for byte as before.

- **What is drawn.** Each element with a `GuiImage`, in the layout's draw order (a parent, then its children in sibling order; later is in front). Without a texture it is a plain rectangle of its tint. A `GuiText` draws nothing yet.
- **Colour.** Authored in sRGB with alpha and blended as it is, in the display-encoded space the target is already in. The texture's colour is multiplied by the tint, so the UI image has to be an sRGB colour texture (the import default).
- **Buttons.** A `GuiButton` on the same entity multiplies the image's tint by its `normal`, `pressed` (while a pointer holds it) or `disabled` (not `interactable`) tint.
- **Nine-slice.** With a non-zero `slice` (left, top, right, bottom in texels) and a texture the image is nine quads: the corners keep the border's size times the canvas scale, the edges and the centre stretch. Borders wider than the rectangle shrink together to fit, and a band with no room is left out.
- **Loading.** An image whose texture is still being imported draws nothing, rather than a white rectangle that would flash, and is asked for again on the next frame.
- **Not clipped.** Elements are not clipped to their parents or to the safe area.

## In the editor

The editor owns a `GuiSystem` like the player's `Game` does ([editor.md](editor.md#game-view)). It lays the canvases out against the Game panel's image every frame, in edit mode as in play mode, so a layout is seen while it is made; draws them into the Game view's target after the scene's passes; and, in play mode with the panel focused, offers the panel's pointer events to the UI before the game. Pausing, stopping, losing the focus or closing the panel calls `GuiSystem::releaseAll`, which lets go of every held pointer without a click. A selected element is outlined over the image. The viewport (the editor's own camera) shows no UI.

## The sample

`apps/samples/basic/scenes/menu.scene.json` is the menu: a `Dim` image that blocks input, a nine-sliced `Menu` panel with a column of three buttons (Play, Options, and a Quit that is not interactable), and an `OptionsPanel` popup that starts `Disabled`. `scripts/toggle_button.lua` shows or hides its `target` entity when its button is clicked, which is how Options opens the popup and Close shuts it. Run it with `sonnet_player apps/samples/basic --scene scenes/menu.scene.json`.

## Tests

`gui_tests` run headless, without a GPU: anchors, offsets and pivots against hand-computed rectangles, row and column layouts with spacing, padding and alignment, the canvas scale for width, height and between, a safe-area inset, canvas order, a disabled subtree hidden and input-less, hit testing over overlapping canvases and siblings, capture of a pointer until its release, one click for a press and release inside and none for a release outside, a finger and its synthesised mouse as one press, a consumed touch absent from `InputState`, the window-to-target mapping, the scene round trip of every component, the quads of images (rectangle, tint, draw order, the button's state tints, a disabled subtree, a texture still loading), the nine-slice rectangles and texture coordinates against hand-computed values, and that the pass is declared only when something draws. `runtime_tests` draws a box through `Game` and finds it in the screenshot; `renderer_tests` reads a quad back from the top-left on Lavapipe, `runtime_tests` clicks the sample menu's buttons through `Game` (Options opens the popup, Close hides it, Quit and the dim layer do nothing, and no click reaches the game's input) and compares a capture of the menu with `tests/golden/menu.png` on Lavapipe, and `editor_tests` lays the UI out against the Game panel in edit mode. `scripting_tests` runs a Lua `onPress`, `onRelease` and `onClick`.
