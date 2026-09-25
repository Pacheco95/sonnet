# ADR-0020: Linux runtime libraries beside the editor

- **Status:** Accepted
- **Date:** 2026-09-25

## Context

Slang's shared library gave every binary above `assets` a RUNPATH into vcpkg's library directory. SDL found the Vulkan loader there before the system's, and that loader was built without window-system support, so the editor could not open a window ([issue #27](https://github.com/Pacheco95/sonnet/issues/27)). The SDK's `LD_LIBRARY_PATH` hid the failure on the development machine. The exported player also depended on Slang under some toolchains despite never calling it.

## Decision

`ShaderCompiler` and its tests live in `editor`, following [ADR-0018](0018-mobile-export.md). Only that module imports and links the Slang library package; the shader build rules find the host `slangc` executable independently.

On Linux, copy the Slang compiler and its loadable modules beside the editor and `editor_tests`, using the imported targets' filenames. Those binaries have RUNPATH `$ORIGIN`, replacing CMake's automatic build RUNPATH. Binaries below the editor have no Slang dependency or search path. No Vulkan loader is copied.

SDL uses its normal lookup to load the distribution's Vulkan loader, or a loader explicitly selected by the user. This supersedes the Linux runtime-loader consequence of [ADR-0004](0004-vcpkg-first.md): vcpkg still supplies build dependencies, including the loader required by the `vulkan` stub port, but its loader is not deployed. The single SDL-owned loader of [ADR-0006](0006-vulkan-object-ownership.md) is unchanged.

## Consequences

- Linux installations need the system Vulkan loader and GPU driver. No loader window-system features are needed in the vcpkg manifest.
- The editor's runtime compiler stays available, while an exported Linux player needs no Slang library.
- `editor_tests` checks the loader retained after `Platform` shuts down. CTest clears the SDK's library path and SDL's explicit loader override so they cannot hide the original failure.
- Slang's runtime copies add disk usage to each editor binary directory. Windows and macOS keep their existing library deployment.

## Alternatives considered

- **Explicit system path in `platform`.** A `dlopen` probe with the original vcpkg RUNPATH selected vcpkg's loader; passing `/lib/x86_64-linux-gnu/libvulkan.so.1` selected the system loader. This works locally but needs a portable path-discovery policy across Linux distributions and architectures, and still exposes every other vcpkg shared library through RUNPATH.
- **Overlay the `vulkan` stub to omit the loader.** A probe with a search directory containing no loader fell through to the system loader. Port inspection shows that this also requires removing the manifest's direct loader dependency and changing the stub's loader discovery. It leaves the broad vcpkg RUNPATH and adds an overlay to maintain. The chosen copy rule isolates exactly the libraries the editor uses without changing third-party ports.
- **Enable window-system features on vcpkg's loader.** PR #28 demonstrated that this opens the window, but it keeps the accidental loader selection and the player's Slang dependency.
