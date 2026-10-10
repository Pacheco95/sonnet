# ADR-0025: File formats at 1.0.0

- **Status:** Proposed
- **Date:** 2026-10-10

## Context

[M14](../roadmap.md#m14-performance-targets-and-10) tags 1.0.0 once the formats have stopped changing, and [conventions.md](../conventions.md#versioning) lists the formats the version protects but not what a version number on them promises. The audit of what the code does today:

| Format | Version (where) | Refuses a newer file | Migrates an older one |
|---|---|---|---|
| Scene | 3 (`SceneVersion`, `world/Scene.h`) | yes, naming both versions | yes, 1 to 2 to 3, each step logged |
| Prefab | the scene's counter: `saveSubtree` writes `SceneVersion` | yes, as a scene | yes, as a scene |
| Material (`.material.json`) | 1 (`MaterialFileVersion`) | yes | none needed yet |
| Sidecar (`.meta`) | 2 (`SidecarVersion`) | **no**: any file with a `uuid` is read | only to rebuild a glTF's sub-asset list when below 2 |
| Project (`project.json`) | **none**: only `engineVersion`, which nothing reads | no | no |
| Cooked bundle | 3 (`BundleVersion`, in the header) | accepts exactly its own version | no |

Not in scope, because they are local state and not shared between machines or projects: the editor's recovery file (version 1), its preferences, and caches such as cooked KTX2 files.

Two gaps follow. A project has no schema version, so a format change to it could not be told apart from an old file. And an engine reads a sidecar from a newer one without complaint, and may then rewrite it and lose fields.

## Decision

1. **From 1.0.0, a 1.x engine reads every schema version any earlier release wrote** for the five source formats (project, scene, prefab, material, sidecar), migrating it on load, oldest step first, and writes only the current version. A newer file than the engine knows is refused with an error naming both versions. A change that cannot be migrated waits for 2.0.0.
2. **A migration step is a function from version N to N+1 and is never removed within a major version.** Each logs the source and both versions, as the scene's do ([conventions.md](../conventions.md#logging)), and each historical version has a test with a fixture, so the chain stays honest.
3. **The project file gets an integer `version`**, 1, written on every save. A file without one is version 1. A newer one is refused.
4. **A sidecar of a newer version is refused**, like the others.
5. **Cooked bundles are build output and are not migrated.** The player reads exactly the version of the engine that built it and refuses any other with an error that names both and says to cook again with this engine. A bundle version bump is a MINOR engine bump, since cook and player ship together and only an exported game that was not cooked again stops working. This is the one exception to "a schema bump ships with a migration" in conventions.md.
6. **`engineVersion` in a project and in a bundle's manifest is informational.** It names the engine that wrote the file and appears in the refusal messages, and it never decides whether a file is readable; the schema version does.
7. **The version numbers on 1.0.0 are the ones above, plus the project's 1:** scene and prefab 3, material 1, sidecar 2, bundle 3.

## Consequences

- Items 3 and 4 are code, and each is a format addition, so they land before the tag as a MINOR bump (0.15.0) with their tests; the audit found no other change the freeze needs.
- A project written by 0.14.0 opens in 1.0.0 with no migration, since a missing `version` means 1.
- The cost of the promise is carrying migrations forever. It is small today (two scene steps) and each future step is a few lines plus a fixture.
- `docs/conventions.md` says 1.0.0 waits for M10; it is M14, and the note about "project and bundle formats" becomes this ADR's list.
- Scripting API and command-line compatibility, the other items conventions.md protects, are not decided here.

## Alternatives considered

- **Migrate bundles too.** A cooked bundle is the engine's own output and can be rebuilt from the project, so a migration would be code kept forever for a file nobody edits, and the cook already redoes everything. It lost on cost.
- **Keep reading only the previous version.** Cheaper, but a project that skips a release would not open, which is not what a 1.0 means.
- **Use `engineVersion` as the compatibility check.** It ties compatibility to the release instead of to the schema, so a patch release would refuse files it can read. It lost to the schema version, which already exists.
- **Give each format its own major version policy.** More precise, with more to remember. One rule for the five source formats is easier to keep.
