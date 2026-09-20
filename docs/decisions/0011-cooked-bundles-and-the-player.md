# ADR-0011: Cooked bundles and the player runtime

- **Status:** Accepted
- **Date:** 2026-09-19

## Context

M6 turns the engine into something that ships a game. [ADR-0007](0007-data-driven-game-structure.md) settled what a game is — a project folder run by a generic player — and left three things open: where the player's frame lives, what a cooked bundle is, and how the player loads one. The editor already runs a whole game in play mode ([editor.md](../editor.md#play-mode)): the same renderer, asset database, world, physics, scripts, animation and audio, driven by the same fixed timestep. The player is that arrangement without the editing, and an exported game has to start on a machine with no SDK, no vcpkg, no project folder and no importers.

The asset database is source-oriented: it scans the project's roots, writes sidecars, imports on first use and cooks textures into `.sonnet/cache` ([assets.md](../assets.md#database)). Everything above it — the draw list, the scripting bindings, the audio device, the animation systems — asks it for assets by UUID and never learns where they came from.

## Decision

- **The runtime is a module, `runtime`, between `audio` and `ui`.** `runtime::Game` owns the renderer, the render graph, the asset database, the world and the subsystems, and exposes `event`, `update` and `render` the way `editor::Editor` does; `apps/player` is the executable that owns the window, device and swapchain and calls them in frame order, as `apps/editor` does. The player's frame is then covered by `runtime_tests` headless on Lavapipe, which an untested `main.cpp` would not be. `Game` opens either a project folder or a bundle and runs the start scene with the world playing from the first frame; there is no edit mode in it and it never links `ui` or `editor`.
- **A bundle is one file.** `<name>.sbundle` is a small header, the payload blobs back to back, and an index at the end: the manifest (project name, engine version, target platform, start scene), the asset entries by UUID with their type, name, parent and default materials, and the file entries by project-relative path for scenes and prefabs. The index is CBOR, so it stays a `nlohmann::json` document at both ends and costs no parser of its own. One file is what gets copied next to a player binary, and reading the index reads one range.
- **Each asset is cooked into the form the loader already wants.** Textures keep the KTX2 the editor caches today, so the cooked path and the edit path are literally the same bytes. Meshes become a binary block of the renderer's vertex layout, welded and reordered for the vertex cache. Skins, clips and models become binary, since their arrays have no JSON form worth inventing. Materials, scenes and prefabs are CBOR of the JSON they already have, so the version field and the migrations keep working unchanged. Scripts and sounds are stored as they are, source text and the encoded file, and are decoded at run time as they are today.
- **The database gets a second mode, not a second class.** `AssetDatabase::openBundle` fills the same index from the bundle's entries, and each loader reads its blob instead of importing; `pollChanges` does nothing. Nothing above `assets` changes, and the player is the editor's play mode with the editing removed rather than a second loading path that can drift from it.
- **Cooking is a library function in `assets`, driven by the database itself.** `assets::cook` opens the project in a database on a null device (`rhi::createNullDevice`), asks for every asset, and writes what comes back. The importers therefore run exactly once, in the code that already runs them; `sonnet_cook` and the editor's export dialog are two front ends of one function, and cooking needs no GPU.
- **The project file belongs to `assets`.** `Project` moves from `editor` to `assets`, because the player reads `project.json` too and [assets.md](../assets.md#project-file) already documents it there. Creating a project and its starter scene stays in the editor, which is the only thing that needs `world` to build one.
- **Export assembles, it does not cross-compile.** Exporting writes the bundle, copies the target's player binary and the compiled engine shaders next to it, and nothing else. Building a player for another platform is CI's job; the editor exports with the player binaries it is given, which for the host is the one beside it.

## Consequences

- An exported game is a directory: a player binary, `shaders/`, the runtime libraries the platform needs, and one `.sbundle`. It starts with no project folder, no importers and no compiler, which is the milestone's criterion.
- The player and the editor resolve assets through one database, so an asset that works in play mode works in the export, and a cooking bug shows up as the same wrong pixels in both.
- Cooking is as slow as importing, because it is importing; it is a whole-project step, not incremental. Bundles are not compressed as a whole, since the two big payload kinds, KTX2 and the sound files, carry their own compression.
- A bundle is not editable. Hot reload, sidecars and re-import belong to project mode, and the player in bundle mode has none of them, which is what a shipped game wants.
- CBOR for scenes means the binary form and the JSON form are the same document, so a cooked scene can be dumped back to JSON for debugging, and a scene format migration costs the cook nothing.
- Equirectangular environment maps are stored uncompressed, since the KTX2 cooking path takes RGBA8 and an environment is RGBA16F. That is the largest thing in a bundle until it is compressed too.
- `runtime` sits below `ui`, so the module order still reads top to bottom and `editor` links it without using it. A later C++ game-module hook (roadmap, "Later") has an obvious place: a custom player linking `runtime`.

## Alternatives considered

- **The player's frame in `apps/player/main.cpp`**: no new module, but the player's frame order, scene camera and bundle loading would have no tests, and `apps/` would hold the one piece of engine behaviour outside `modules/`.
- **A directory of cooked files instead of one bundle**: easier to inspect, but exporting becomes a copy of thousands of files, and the loader needs a manifest anyway.
- **A custom binary encoding for scenes**: more compact than CBOR, but it would duplicate the serializer and its migrations, and scene data is a small part of a bundle next to textures and meshes.
- **A second database class, or an `IAssetSource` behind the current one**: a cleaner seam on paper, but the two modes differ only in where bytes come from, and the interface would have to carry the source mode's sidecars, settings and re-import to stay useful to the editor.
- **A cook tool with its own importers, independent of the database**: no null device needed, but a second copy of the import rules — sub-asset identities, texture colour spaces, default materials — which is exactly the drift ADR-0007 wants avoided.
- **FlatBuffers or protobuf for the bundle**: schema evolution for free, but a new dependency and a code generator in the build for a format only this engine reads.
