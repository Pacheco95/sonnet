# world

The entity component system: a flecs world with the engine's components, phases and systems, the scene and prefab files, and the draw list handed to the renderer. flecs types appear unwrapped in the public headers by decision ([ADR-0003](decisions/0003-ecs-library.md)). Depends on `renderer`, flecs and nlohmann-json; the `assets` module slots in between in M3.

| Header | Contents |
|---|---|
| `Components.h` | The core components as plain structs: `Identity`, `Name`, `Transform`, `WorldTransform`, `MeshRenderer`, `Camera`, the three lights, `Spin`, and the tags `Static`, `EditorOnly`, `Disabled` |
| `World.h` | `World`: the flecs world with everything registered, entity creation with identities, hierarchy helpers, prefab instantiation, the component registry, JSON per component, play mode and `progress` |
| `Scene.h` | The scene and prefab file format: `saveScene`, `loadScene`, `savePrefab`, `loadPrefab` and their file variants |
| `DrawList.h` | `PrimitiveMeshes`, `buildDrawList`, `sceneLight`, `sceneCamera`: what the world hands the renderer |

## Components

Every component is registered with flecs reflection in `World`, under the name scene files use, so the inspector, the serializer and later the scripting bindings enumerate fields through one system. GLM's `vec3`, `vec4` and `quat` are registered as structs; `Primitive` is an enum whose constants serialize by name; angles carry the flecs `Radians` unit, which the inspector reads to show degrees ([conventions.md](conventions.md#math-conventions)).

Three components are structural and are not in the registry: `Identity` holds the UUID that scenes, prefab references and undo refer to; `WorldTransform` is derived; `Name` is written as the file envelope's `name`. `World::createEntity` gives every scene entity all three plus a `Transform`.

`Transform` is local. The transform system, in the `PreRender` phase, computes `WorldTransform` as parent times local, parents first through the flecs `cascade` ordering over `ChildOf`. `World::setParent` keeps the world transform when reparenting, so an entity stays where it is on screen. `Transform::fromMatrix` decomposes a matrix back, discarding shear.

Until assets arrive in M3, `MeshRenderer` names one of the renderer's primitives and a colour. `Spin` turns its entity about an axis in play mode and is the script-free behaviour M2's sample uses. `PointLight` and `SpotLight` are authored and saved now and rendered from M3.

## Phases and play mode

The phases `Input`, `FixedUpdate`, `Update`, `PostUpdate` and `PreRender` are flecs phase entities in a dependency chain; systems name theirs with `kind(world.phase(...))`. Systems tagged `Simulation` (the spin system today, physics and scripts later) belong to play mode: `World` keeps two pipelines, one without that tag, and `setPlaying` switches between them, so `progress(dt)` runs the simulation only while playing and the transform system always. The fixed timestep of the architecture's frame description arrives with physics in M4, when something needs it.

`Disabled` is the engine's tag, not flecs' built-in one, so a disabled entity still appears in the hierarchy; the draw list and the simulation systems skip it.

## Hierarchy

Hierarchy is flecs `ChildOf`. `roots` and `children` return entities sorted by id, since flecs table order changes as components are added and a panel needs a stable order; the id order is creation order until ids are recycled. `destroyEntity` deletes the subtree. `clearScene` deletes every scene root and keeps prefabs and editor-only entities.

The low 32 bits of a flecs id are the pick id the id pass writes ([rendering.md](rendering.md#the-renderer-module-today)); `fromPickId` resolves it back to the live entity.

## Prefabs

A prefab is a flecs prefab entity with an `Identity`, loaded from a `.prefab.json` file, and an instance is an entity that `IsA` it. Registered components are inherited (`OnInstantiate, Inherit`): an instance shares the prefab's values until it writes a component, which overrides it. `Identity` and `WorldTransform` are never inherited; `instantiate` gives the instance its own identity, name and transform, and gives the instantiated children identities and world transforms of their own. The scene stores an instance as its prefab's UUID plus the components it owns, and not the children, which come back with the prefab. Children added under an instance by hand are therefore not saved in M2; nested scene instances beyond prefabs are listed under "Later" in the roadmap.

## Scenes

Scenes and prefabs share one JSON format ([assets.md](assets.md#scene-file)), an engine-owned envelope around flecs' JSON for each component value:

```json
{
  "version": 1,
  "entities": [
    {
      "uuid": "6f1c...", "name": "Box", "parent": "a3e9...",
      "components": {
        "Transform": { "position": {"x": 0, "y": 0.5, "z": 0}, "rotation": {"x": 0, "y": 0, "z": 0, "w": 1}, "scale": {"x": 1, "y": 1, "z": 1} },
        "MeshRenderer": { "primitive": "Box", "color": {"x": 0.9, "y": 0.35, "z": 0.25, "w": 1}, "visible": true },
        "Static": null
      }
    },
    { "uuid": "b7d2...", "name": "Crate 1", "prefab": "c0ff...", "components": { "Transform": { "position": {"x": 4, "y": 0, "z": 0} } } }
  ]
}
```

Entities are written parents first, tags as `null`, and editor-only entities not at all. Loading creates every entity, then resolves parents, so the order in the file does not matter; a load that fails part way removes what it created and reports why, with the UUID of a missing prefab or parent. A prefab an instance refers to has to be loaded first, which the editor does for every `.prefab.json` in the project. Unknown component names are logged and skipped.

`version` is the schema version, independent of the engine version ([conventions.md](conventions.md#versioning)). A newer version than the engine knows is refused; older ones are migrated on load, oldest first, and never written back. Version 1 is the first, so no migration exists yet.

## Draw list

`buildDrawList` fills `renderer::DrawItem`s from every entity with a `WorldTransform` and a visible, enabled `MeshRenderer`, with the entity's pick id. `sceneLight` is the first directional light, shining along its entity's -Z; `sceneCamera` the first camera, placed by its entity's world transform. The editor draws through its own camera and uses the scene's light; the player uses both.

## Debugging

`WorldDesc::explorer` imports the flecs statistics module and serves the REST API, so the [flecs explorer](https://www.flecs.dev/explorer) shows the live world. The editor turns it on in Debug builds.

## Tests

`world_tests` covers the transform decomposition, JSON round trips by reflection including enums, units and tags, the hierarchy and world transforms, reparenting, play mode gating, prefab instantiation and overrides, scene and prefab files through the temporary directory including the failure paths, and the draw list on the null device.
