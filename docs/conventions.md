# Conventions

Rules that apply across modules. Module-specific rules live in that module's documentation.

## Code style

Carried over from the first iteration of the engine and extended.

- Format with clang-format using the repository `.clang-format` (LLVM style, 120 columns, no single-line functions or enums); CI rejects unformatted code. The file lists only the differences from LLVM style so it parses with clang-format 18 and newer.
- `#pragma once` in every header. No include-guard macros.
- One class per header, `.h` and `.cpp` pairs. Header-only is allowed for templates and small value types.
- Includes ordered: the matching header, then the module's own headers, then other Sonnet modules, then third party, then the standard library. Sonnet headers use angle brackets with the `sonnet/<module>/` prefix.
- Namespaces: `sonnet::<module>`. No `using namespace` in headers.
- Types, namespaces and enum values: `PascalCase`. Functions and variables: `lowerCamelCase`. Constants and enumerators: `PascalCase`. Macros: `SONNET_UPPER_CASE`, avoided outside `core`.
- Member variables of classes with behaviour: `m_` prefix. Fields of plain data structs: no prefix. Globals: `g_`. Statics: `s_`.
- Interfaces (classes with only pure virtual functions): `I` prefix, e.g. `IWindow`, `IDevice`. Implementations carry the technology name: `SdlWindow`, `VulkanDevice`.
- Prefer `enum class`, `std::span`, `std::string_view`, `std::optional`, `std::expected`, `constexpr`. No raw `new`/`delete` outside allocators; ownership is `std::unique_ptr` or a handle.
- Comments explain why, not what: constraints, ordering requirements, third-party quirks, non-obvious math. No comment that restates the code.
- No backward-compatibility shims, feature flags or future-proofing ahead of need. When something changes, change it and update the callers.
- Third-party code is never modified in place; patches go through vcpkg overlay ports.

## Files and naming

- Sources: `Transform.h`, `Transform.cpp`, `TransformTests.cpp`.
- Shaders: `<pass>.slang` for entry-point files, `<name>.slang` for modules, all in `modules/renderer/shaders/` or a project's `shaders/` folder.
- Scenes and prefabs: `<name>.scene.json`, `<name>.prefab.json`. Asset sidecars: `<file>.meta`.
- CMake targets: `sonnet_<module>` with alias `sonnet::<module>`; tests `<module>_tests`; apps `sonnet_editor`, `sonnet_player`, `sonnet_cook`.

## Math conventions

- Right-handed, +Y up, +Z toward the viewer, cameras look down -Z. This matches glTF and GLM defaults.
- Units: metres, seconds, radians, kilograms. Angles in public APIs are radians; the inspector converts to degrees for display.
- GLM is configured once in `core`: `GLM_FORCE_RADIANS`, `GLM_FORCE_DEPTH_ZERO_TO_ONE`, `GLM_FORCE_EXPLICIT_CTOR`, `GLM_ENABLE_EXPERIMENTAL` for quaternion helpers. No other translation unit defines GLM macros.
- Matrices are column-major, transforms compose as `parent * local`, and a `Transform` stores position, quaternion rotation and scale. World matrices are derived, never edited directly.
- Rendering uses reversed-Z with an infinite far plane and a negative viewport height for the Vulkan Y flip, so front faces are counter-clockwise everywhere in engine code.

## Error handling

- Initialization and resource creation may throw. Engine exceptions derive from `core::Exception`, which records the throw site as a `std::source_location`. A thrown exception during startup terminates with a logged message that includes that site; there is nothing sensible to do instead.
- The per-frame path never throws and never allocates on the hot path without a reason noted in a comment.
- Recoverable operations return `std::expected<T, Error>`: loading an asset, compiling a shader, opening a project, parsing a scene. `Error` carries a message, a category and the `std::source_location` of its creation, filled in by default. Callers, usually the editor, display the error with that origin and continue.
- Vulkan-HPP keeps its default exception behaviour. Calls that can fail at runtime for legitimate reasons (swapchain out of date, device lost) are handled through their result codes with the no-throw overloads.
- `SONNET_ASSERT` is active in Debug and RelWithDebInfo, compiled out in Release. `SONNET_VERIFY` always evaluates its expression. Assertions are for programmer errors, never for bad input data.

## Logging

- spdlog through `core`, one logger per module named after the module (`core`, `rhi`, `assets`, ...).
- Levels: `trace` for per-frame noise (off by default), `debug` for lifecycle events, `info` for user-visible progress, `warn` for recoverable problems, `error` for failures that were handled, `critical` before termination.
- Every log call goes through the `SONNET_LOG_TRACE`, `SONNET_LOG_DEBUG`, `SONNET_LOG_INFO`, `SONNET_LOG_WARN`, `SONNET_LOG_ERROR` and `SONNET_LOG_CRITICAL` macros in `core`. They capture the calling file, line and function and hand them to spdlog as the record's source location. Calling spdlog directly is not allowed because it loses the location.
- Loggers flush from `debug` up, so lifecycle lines survive a crash or a kill when stdout is a pipe; only `trace` is buffered.
- Line format: `[time] [level] [module] [file:line] message`. Example: `[12:03:45.123] [warn] [assets] [GltfImporter.cpp:212] hero.gltf: mesh "Blade" has no tangents, generating them`. The file is the base name; the full path and the function name stay on the record for sinks that want them.
- When a message is about something that has its own source, that location is shown instead of, or next to, the C++ one: script log calls report the script file and line from the Lua debug information, shader diagnostics report the `.slang` file, line and column from the compiler, asset problems name the asset path and the JSON line where there is one, and format migrations name the file and both schema versions.
- `Error` values and engine exceptions capture the `std::source_location` where they are created, so the line that reports them shows the origin of the problem, not the place where it was logged. A failed assertion logs the expression, file, line and function before breaking into the debugger.
- Vulkan validation messages have no source location. They are forwarded with their severity mapped onto the levels above, and carry the VUID, the message id and the object names involved, which the debug-utils names make readable.
- Locations are compiled in at every level. `trace` and `debug` calls compile out of Release builds through `SPDLOG_ACTIVE_LEVEL`. The build maps the source root to a relative prefix, so file names are repository-relative on every platform and no build-machine paths end up in shipped binaries.
- The editor log panel subscribes as a sink. A `file:line` entry there is a link that opens the file at that line in the external editor configured in the preferences, for engine sources, scripts and shaders alike.

## Profiling

Tracy zones (`SONNET_ZONE()`) on every system, pass and import step. GPU zones per render-graph pass. Frame markers at present. Zones compile out when `SONNET_ENABLE_TRACY` is off.

## Testing

- Every module has a `tests/` directory built through `sonnet_add_module_test`. A module without tests is an exception that has to be justified in its documentation.
- Tests are Catch2 `TEST_CASE`s grouped by tag with the module name. White-box tests may include the module's `src/`.
- Rendering tests use the null `rhi` implementation, or Lavapipe when the real implementation is under test.
- A bug fix comes with the test that would have caught it.

## Commits

Commit messages follow [Conventional Commits 1.0.0](https://www.conventionalcommits.org/en/v1.0.0/):

```
<type>(<scope>): <description>

<body>

<footers>
```

- Types: `feat` (new capability), `fix` (bug fix), `perf` (performance change with no behaviour change), `refactor` (no behaviour change), `docs`, `test`, `build` (CMake, vcpkg, toolchains), `ci`, `style` (formatting only), `chore` (maintenance that fits nothing else), `revert`.
- Scope is the module or app touched: `core`, `platform`, `rhi`, `renderer`, `assets`, `world`, `physics`, `scripting`, `audio`, `ui`, `editor`, `player`, `cook`, `samples`, plus `cmake`, `deps` and `docs` for cross-cutting changes. Omit the scope when a change spans several modules.
- Description: imperative mood, lowercase, no trailing period, subject line at most 72 characters. Example: `feat(rhi): adopt vk-bootstrap handles into RAII wrappers`.
- Body: explains why, wrapped at 72 columns. Required whenever the reason is not obvious from the subject.
- Footers: `BREAKING CHANGE: <what changed and how to migrate>`, `Refs: ADR-0006`, `Fixes #42`, `Co-Authored-By: ...`. A breaking change also puts `!` after the type or scope: `feat(assets)!: reference assets by UUID instead of path`.
- One logical change per commit. Fix-up commits are squashed before merge so the history reads as a changelog.
- A `commit-msg` hook validates the format locally, and CI validates every commit on a pull request.

## Versioning

The engine follows [Semantic Versioning 2.0.0](https://semver.org/spec/v2.0.0.html): `MAJOR.MINOR.PATCH`, with optional pre-release (`1.2.0-rc.1`) and build metadata (`1.2.0+g1a2b3c`).

The public API that versioning protects:

- The public headers of every module under `sonnet/<module>/`.
- The project, scene, prefab and asset sidecar file formats, and the cooked bundle format.
- The scripting API exposed to Lua.
- The command-line interfaces of `sonnet_editor`, `sonnet_player` and `sonnet_cook`.

Rules:

- Before 1.0.0, in the `0.y.z` range: each roadmap milestone landing bumps MINOR, so M0 ships as 0.1.0 and M7 as 0.8.0. A MINOR bump may break the public API. PATCH is for fixes only. 1.0.0 is tagged after M7, once the project and bundle formats have stopped changing.
- From 1.0.0: a breaking change bumps MAJOR, a new capability bumps MINOR, fixes and performance changes bump PATCH.
- Commit types decide the bump: `feat` bumps MINOR, `fix` and `perf` bump PATCH, any `!` or `BREAKING CHANGE:` footer bumps MAJOR (MINOR before 1.0.0). Other types never bump on their own.
- File formats carry their own integer schema version, independent of the engine version. A schema bump ships with a migration and is at least a MINOR bump of the engine. The engine never writes an old schema version.
- The version is declared once, in `project(sonnet VERSION ...)` in the root `CMakeLists.txt`. `vcpkg.json` mirrors it and CI fails when they differ. `core` exposes it at runtime, and `project.json` records the `engineVersion` a project was last saved with.
- Releases are git tags `vMAJOR.MINOR.PATCH` on `main`, created by the release workflow, which also generates `CHANGELOG.md` from the conventional commits since the previous tag.

## Documentation

- Each module has a page under `docs/` or a section in an existing page, kept current with the code in the same change.
- Decisions that affect more than one module get an ADR in `docs/decisions/`, numbered sequentially, using the template there. An ADR is never edited after acceptance except to change its status; a new ADR supersedes it.
- The README stays short: goals, platforms, stack, architecture summary, index.
