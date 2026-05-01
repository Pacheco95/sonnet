# Research: GitHub Actions CI with PR Test Gates

**Branch**: `002-github-actions-ci` | **Date**: 2026-05-01 | **Plan**: [plan.md](plan.md)

---

## Ubuntu Package Verification

**Result**: Verified on Ubuntu 24.04 (via `/tmp/sdl3-check` configure run).

`libvulkan-dev` provides Vulkan headers at `/usr/include/vulkan/` and the loader at `/usr/lib/x86_64-linux-gnu/libvulkan.so`. `find_package(Vulkan REQUIRED)` resolves successfully with these packages. `glslc` (from the `glslc` apt package or LunarG SDK) is found by `ShaderCompilation.cmake`'s `find_program` search since it lands in PATH. Both packages together are sufficient to configure, build, and link the project on a fresh Ubuntu 24.04 environment.

## SDL3 Build Dependencies

**Verified package list** (identified from SDL3 3.4.4 configure output on Ubuntu 24.04):

```
libx11-dev libxext-dev        # X11 display backend
libwayland-dev wayland-protocols  # Wayland display backend
libxkbcommon-dev              # Keyboard handling (X11 + Wayland)
libegl-dev                    # EGL (required for Wayland)
libasound2-dev                # ALSA audio backend
libpulse-dev                  # PulseAudio audio backend
```

SDL3 dynamically loads all backend libraries at runtime (`libSDL3.so` links only `libc` and `libm`), so only development headers are required at build time. All six packages above are needed for SDL3 to compile with a complete feature set; missing packages cause SDL3 to build with those subsystems disabled (not a compile error, but a functional regression from local developer builds).

---

## Runner OS Selection

**Decision**: `ubuntu-latest` (Ubuntu 24.04)  
**Rationale**: Ubuntu 24.04 ships GCC 13 (C++23 support), CMake 3.28+ (upgradable to 4.x via pip or apt), and has the most complete apt package registry for Vulkan headers and SDL3 build dependencies. It is the lowest-friction starting point for this project's stack.  
**Alternatives considered**:
- `ubuntu-22.04`: GCC 11 (C++23 partial) — insufficient for `-std=c++23` without extra setup.
- `macos-latest`: Would also work but macOS runners are slower and billed at 10× the rate of Linux runners. Deferred to a future matrix build feature.
- `windows-latest`: Requires MSVC or MinGW setup and different apt equivalents. Deferred.

---

## Vulkan SDK Installation Strategy

**Decision**: Install `libvulkan-dev` + `glslc` from Ubuntu system packages rather than the full LunarG Vulkan SDK.  
**Rationale**: The CI test suite is entirely headless mock-based (Constitution Principle V); no Vulkan runtime is invoked at test time. Headers and the loader (`libvulkan.so`) from `libvulkan-dev` satisfy `find_package(Vulkan REQUIRED)`. The `glslc` package provides the SPIR-V shader compiler that `ShaderCompilation.cmake` uses, satisfying `Vulkan::glslc`. Installing the full LunarG SDK (~1 GB) would add significant setup time with no benefit to headless CI.  
**Alternatives considered**:
- Full LunarG SDK via PPA: Provides validation layers + additional tools, but bloats setup time and is unnecessary for headless tests.
- Skip Vulkan installation: Would cause `find_package(Vulkan REQUIRED)` to fail at configure time, preventing any build.

---

## SDL3 Build Dependencies

**Decision**: Install the minimum set of Linux development headers needed for SDL3 to compile from source via FetchContent.  
**Rationale**: SDL3 is fetched and built from source (no pre-built Ubuntu package pinned to 3.4.4 exists at the required version). SDL3's CMake build auto-detects available subsystems; missing optional headers simply disable those backends. The subset below covers X11, Wayland, EGL, and audio — sufficient for SDL3 to compile a functional library on Ubuntu 24.04.  
**Packages required**:
```
libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev
libxinerama-dev libxkbcommon-dev
libwayland-dev wayland-protocols
libegl1-mesa-dev libgles2-mesa-dev
libpulse-dev libasound2-dev
libudev-dev
```
**Alternatives considered**:
- Pre-built SDL3 from a PPA: No official PPA exists for SDL3 3.4.4 at the time of writing.
- Build SDL3 with all features disabled (`SDL_DISABLE_EVERYTHING`): Fragile and diverges from how contributors build locally.

---

## CMake Version on Runner

**Decision**: Install CMake 4.2.1 via pip (`pip install cmake==4.2.1`) after runner setup.  
**Rationale**: `ubuntu-latest` ships CMake 3.28 as of Ubuntu 24.04; project requires CMake 4.2.1 minimum. Pip provides the exact pinned version without requiring a PPA or manual binary download.  
**Alternatives considered**:
- Kitware CMake APT repository: Valid alternative, but pip is simpler for exact version pinning in CI.
- Accept CMake 3.28: Would fail `cmake_minimum_required(VERSION 4.2.1)` at configure time.

---

## Dependency Caching Strategy

**Decision**: Cache the FetchContent download directory `build/_deps` keyed on a hash of `cmake/FetchDependencies.cmake` and `CMakeLists.txt`.  
**Rationale**: All four FetchContent dependencies (SDL3, spdlog, Catch2, Vulkan-Hpp) download and build from source, which dominates CI cold-start time. Caching the `_deps` directory across runs with a content-addressed key (invalidated only when dependency declarations change) reduces subsequent run times significantly while remaining correct under version updates.  
**Cache configuration**:
```yaml
key: ${{ runner.os }}-cmake-deps-${{ hashFiles('cmake/FetchDependencies.cmake', 'CMakeLists.txt') }}
restore-keys: |
  ${{ runner.os }}-cmake-deps-
```
**Alternatives considered**:
- Cache the entire `build/` directory: Larger cache artifact; object files and linked binaries grow the cache without proportional benefit since source changes require recompilation anyway.
- No caching: Cold builds may approach the 30-minute timeout for the FetchContent downloads + compilation combined.

---

## Draft PR Exclusion

**Decision**: Use `github.event.pull_request.draft == false` as a job condition; additionally limit workflow triggers to `types: [opened, synchronize, reopened, ready_for_review]`.  
**Rationale**: The spec explicitly excludes draft PRs from CI (FR-001). Using both the trigger type filter and the job condition ensures: (a) CI is not queued at all for new draft PRs, and (b) if a PR is converted from draft to ready-for-review, the `ready_for_review` event type triggers CI at that moment.  
**Alternatives considered**:
- Only job condition (`if: !github.event.pull_request.draft`): Workflow still gets queued for draft pushes; the condition simply skips the job. Less efficient — wastes a workflow run slot.
- Branch filter only (no draft check): Would run CI on all PR updates including draft, violating FR-001.

---

## Status Check Name and Branch Protection

**Decision**: Name the workflow job `build-and-test`; configure the GitHub branch protection rule to require this exact status check name.  
**Rationale**: The branch protection rule references the job name as reported by GitHub Actions. Documenting this as a contract (see `contracts/ci-workflow.md`) prevents the job from being renamed without updating the branch protection rule — which would silently remove the merge gate.  
**Branch protection settings**:
- Require status checks to pass before merging: ✅ `build-and-test`
- Require branches to be up to date before merging: ✅
- Include administrators: ✅ (spec SC-002, assumption 3)
- Allow force pushes: ❌
- Allow deletions: ❌

---

## Build Configuration for CI

**Decision**: Configure CMake with `-DCMAKE_BUILD_TYPE=Debug`.  
**Rationale**: Debug builds activate `sonnet_set_compile_options()` with `-Wall -Wextra -Wpedantic -Werror` and enable Vulkan validation layers via `SONNET_ENABLE_VALIDATION_LAYERS=ON`. This matches Constitution Principle IV and catches the most issues. Release builds (optimized, no validation) are not the appropriate gate for correctness testing.  
**Alternatives considered**:
- Release build: Validation layers off; some warnings may be suppressed by optimizer. Insufficient as a quality gate.
- Both Debug and Release in a matrix: Adds value but doubles CI time; deferred to a future enhancement.

---

## Test Artifact Retention

**Decision**: Upload `build/Testing/` as a GitHub Actions artifact named `test-results` with `retention-days: 7`.  
**Rationale**: CTest writes XML test results to `build/Testing/` via CDash-compatible output. Uploading this directory satisfies FR-008 (logs + reports accessible from PR for 7 days). GitHub's artifact viewer links directly from the Actions run summary reachable from the PR checks section.  
**Alternatives considered**:
- Upload raw log files: Less structured; CTest XML is more tool-friendly.
- 30-day retention: Longer than the typical PR review cycle; wastes storage quota.
