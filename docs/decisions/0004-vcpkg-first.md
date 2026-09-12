# ADR-0004: vcpkg first, FetchContent for edge cases

- **Status:** Accepted
- **Date:** 2026-09-12

## Context

The previous iteration fetched every dependency with CMake `FetchContent`. Clean builds rebuilt Assimp, Jolt and the rest from source every time, third-party targets needed per-target warning fixes in the engine's CMake files, and the Vulkan SDK still had to be installed by hand. The original draft of this README proposed splitting dependencies between `FetchContent` for "pure CMake" libraries and vcpkg for "complicated" ones, but every library on the list is a vcpkg port, including Dear ImGui's docking branch (`imgui[docking-experimental]`), Slang (`shader-slang`) and vk-bootstrap, so the split would have had nothing on one side while keeping two mechanisms and the risk of the same library being provided twice.

## Decision

Dependencies come from vcpkg in manifest mode with a pinned baseline. `FetchContent` is allowed only for a library that has no vcpkg port, or for a pinned fork carrying local patches when an overlay port would be more work than it is worth. A library is never provided by both mechanisms, and a `FetchContent` dependency moves to vcpkg as soon as a port exists. Local modifications to third-party code go through overlay ports, never through edited copies.

## Consequences

- One `vcpkg.json` describes the dependency set; CI uses binary caching so dependencies build once per baseline.
- Android and iOS builds use vcpkg's `arm64-android` and `arm64-ios` triplets, so the mobile milestone does not need a second dependency mechanism.
- Developers need vcpkg installed and `VCPKG_ROOT` set; the presets handle the rest.
- Header and loader for Vulkan come from vcpkg; the Vulkan SDK is only needed on developer machines for validation layers and tools.

## Alternatives considered

- **FetchContent for everything**: known cost from the previous iteration.
- **The originally proposed split**: no library on the current list would use the `FetchContent` side; two mechanisms for zero benefit.
- **Conan**: comparable; vcpkg was chosen for its CMake integration and first-party Android and iOS triplets.
