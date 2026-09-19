# world

The entity component system: a flecs world with the engine's components, phases and systems, the scene and prefab files, and the draw list handed to the renderer. flecs types appear unwrapped in the public headers by decision ([ADR-0003](decisions/0003-ecs-library.md)). Depends on `assets`, flecs and nlohmann-json.

| Header | Contents |
|---|---|
| `Components.h` | The core components as plain structs: `Identity`, `Name`, `Transform`, `WorldTransform`, `MeshRenderer`, `Camera`, the three lights, `Environment`, `Spin`, and the tags `Static`, `EditorOnly`, `Disabled` |
| `World.h` | `World`: the flecs world with everything registered, entity creation with identities, hierarchy helpers and world matrices, prefab instantiation, the component registry open to subsystems, JSON per component, play mode, the fixed timestep and `progress` |
| `Scene.h` | The scene and prefab file format: `saveScene`, `loadScene`, `savePrefab`, `loadPrefab` and their file variants, and `loadModelPrefab` for a glTF file's hierarchy |
| `DrawList.h` | `buildDrawList`, `buildLightList`, `sceneLight`, `sceneCamera`, `sceneEnvironment`: what the world hands the renderer |

## Components

Every component is registered with flecs reflection in `World`, under the name scene files use, so the inspector, the serializer and later the scripting bindings enumerate fields through one system. Subsystems register theirs the same way through `World::registerComponent`, which returns the flecs component to add members to and makes it inheritable by prefab instances (ADR-0009). GLM's `vec3`, `vec4` and `quat` are registered as structs; `core::Uuid` is an opaque type that serializes as its canonical string, which is how asset references appear in files; angles carry the flecs `Radians` unit, which the inspector reads to show degrees ([conventions.md](conventions.md#math-conventions)).

Three components are structural and are not in the registry: `Identity` holds the UUID that scenes, prefab references and undo refer to; `WorldTransform` is derived; `Name` is written as the file envelope's `name`. `World::createEntity` gives every scene entity all three plus a `Transform`.

`Transform` is local. The transform system, in the `PreRender` phase, computes `WorldTransform` as parent times local, parents first through the flecs `cascade` ordering over `ChildOf`; `World::worldMatrix` computes one entity's from the hierarchy directly, for code that runs before the transform system in a frame. `World::setParent` keeps the world transform when reparenting, so an entity stays where it is on screen. `Transform::fromMatrix` decomposes a matrix back, discarding shear.

`MeshRenderer` references a mesh asset by identity ([assets.md](assets.md#identity)), a built-in primitive or a glTF mesh, with an optional material that overrides every slot of the mesh (nil keeps the mesh's own materials) and a colour that multiplies the material's base colour. `PointLight` and `SpotLight` are punctual lights at their entity's position, the spot shining along its -Z. `Environment` names the equirectangular map that lights the scene and fills its background; the first entity that has one wins. `Spin` turns its entity about an axis in play mode and is the script-free behaviour M2's sample uses.

## Phases and play mode

The phases `Input`, `FixedUpdate`, `Update`, `PostUpdate` and `PreRender` are flecs phase entities in a dependency chain; systems name theirs with `kind(world.phase(...))`. Systems marked with `addToSimulation` (the spin system) belong to play mode; the transform system runs always.

`progress(dt)` runs the phases as three flecs pipelines inside one flecs frame: `Input`, then `FixedUpdate` as many times as the accumulated time holds whole fixed steps, then `Update`, `PostUpdate` and `PreRender`. The first and last pipelines come in an edit and a play variant, the play one with the simulation systems, and `setPlaying` picks between them; the fixed pipeline runs only in play mode. The fixed step is `WorldDesc::fixedDelta`, 1/60 s by default, and systems in `FixedUpdate` see it as their delta time. A frame runs at most `maxFixedSteps` steps, four by default, and drops the rest of its backlog, so a long frame slows the simulation down instead of making the next frame longer still. `fixedAlpha` is the fraction of a step left in the accumulator, for interpolating what is drawn; starting play resets the accumulator.

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

`version` is the schema version, independent of the engine version ([conventions.md](conventions.md#versioning)). A newer version than the engine knows is refused; older ones are migrated on load, oldest first, logging the file and both versions, and never written back. Version 2 references meshes by asset identity; version 1 named a primitive (`"primitive": "Box"`), which the migration turns into the built-in mesh of the same shape.

A glTF file is a prefab too: `loadModelPrefab` builds one from the file's `assets::Model`, the root under the model's identity and name, every node a prefab child with its transform and, for a node with geometry, a `MeshRenderer` of its mesh, with identities derived from the model's so an instance saves and loads the same way after a re-import. The editor loads one for every model in the project alongside the `.prefab.json` files.

## Draw list

`buildDrawList` fills `renderer::DrawItem`s from every entity with a `WorldTransform` and a visible, enabled `MeshRenderer`: one item per submesh, the mesh and materials resolved through the `AssetDatabase` every frame so a re-imported asset shows on the next one, and an entity whose mesh is missing or failed to import draws nothing. `buildLightList` collects the enabled point and spot lights; `sceneLight` is the first directional light, shining along its entity's -Z; `sceneCamera` the first camera, placed by its entity's world transform; `sceneEnvironment` the first `Environment` whose map loads. The editor draws through its own camera and uses the scene's lights and environment; the player uses the camera too.

## Debugging

`WorldDesc::explorer` imports the flecs statistics module and serves the REST API, so the [flecs explorer](https://www.flecs.dev/explorer) shows the live world. The editor turns it on in Debug builds.

## Tests

`world_tests` covers the fixed timestep's step count, order, remainder and backlog limit, components registered from outside the world, the transform decomposition, JSON round trips by reflection including identities, units and tags, the hierarchy and world transforms, reparenting, play mode gating, prefab instantiation and overrides, scene and prefab files through the temporary directory including the failure paths, the version 1 migration, model prefabs, and the draw and light lists on the null device.
