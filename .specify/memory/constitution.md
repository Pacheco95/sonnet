<!--
## Sync Impact Report

- **Version change**: 1.0.0 → 2.0.0
- **Modified principles** (all redefined with project-specific, actionable rules):
  - I. Modularity: generic OOP module rules → CMake static library architecture, PRIVATE dependency
    isolation, uint64_t opaque handle pattern, no graphics API types in public headers
  - II. Usability: general UX/API rules → graceful init failure with human-readable errors, spdlog
    file+line in every log line, README-first onboarding guarantee
  - III. Stability: interface versioning rules → FetchContent pinned GIT_TAG, Vulkan SDK via
    find_package, vendor/ reservation, CMAKE_CXX_STANDARD_REQUIRED ON + CMAKE_CXX_EXTENSIONS OFF
  - IV. Predictability: determinism/side-effect rules → sonnet_set_compile_options() discipline,
    Vulkan validation layers in Debug, SDL3 callback API lifecycle
  - V. Testability: general test isolation rules → I-prefixed abstract interfaces, mock-only tests
    without display/GPU/SDL3, catch_discover_tests() per module
  - VI. Portability: platform-neutral abstraction rules → SDL3+Vulkan cross-platform stack,
    prerequisites documented in README+quickstart
  - VII. Internationalization: strict i18n rules → exception-based model with Complexity Tracking
    table; hardcoded strings permitted with explicit written justification
- **Modified sections**:
  - Quality Standards: added engine performance baselines (FPS, RAM, VRAM, CPU, exit time, leaks)
  - Development Workflow: added mandatory stack (C++23, CMake 4.2.1, spdlog macros, Catch2)
- **Removed sections**: None
- **Templates reviewed**:
  - `.specify/templates/plan-template.md`: Constitution Check gate is derived at plan time from this
    file — no template changes required ✅
  - `.specify/templates/spec-template.md`: Requirement and acceptance scenario structure aligns with
    updated principles — no changes required ✅
  - `.specify/templates/tasks-template.md`: Phase structure and task categories are compatible with
    all 7 updated principles — no changes required ✅
  - `README.md`: Not present yet — skipped ✅
- **Deferred TODOs**: None
-->

# Sonnet Constitution

## Core Principles

### I. Modularity

Each engine domain MUST be implemented as an independent CMake static library with a clearly named
target (e.g., `SonnetLogging`, `SonnetWindow`, `SonnetRenderer`, `SonnetRendererVulkan`).
Dependencies between modules MUST flow in one direction only — no cycles are permitted.

Third-party dependencies fall into two categories with different visibility rules:

- **Backend/platform types** — types from libraries that are swappable implementation details
  (SDL3, Vulkan, Vulkan-Hpp, spdlog, and any future platform-specific backend library) MUST be
  declared `PRIVATE` in CMake `target_link_libraries` and MUST NOT appear in any module's public
  headers. Graphics API object handles exposed across module boundaries MUST use opaque `uint64_t`
  rather than their native types.
- **Shared utility types** — types from libraries that form stable API vocabulary shared across
  modules (e.g., GLM math types, Boost containers) MAY appear in public headers and MUST be
  declared `PUBLIC` or `INTERFACE` in CMake. Such libraries MUST be explicitly designated as
  "shared utilities" in the `FetchDependencies.cmake` comment to distinguish them from backend
  dependencies.

**Rationale**: The restriction targets swappable implementation details — leaking a `VkInstance`
into a public header couples every consumer to Vulkan forever. Utility types like `glm::vec3` are
the stable vocabulary of the API itself; hiding them behind wrappers would produce redundant
type aliases with no isolation benefit.

### II. Usability

If any engine subsystem fails to initialize, the engine MUST emit a human-readable error message
identifying what failed and MUST exit gracefully with a non-zero exit code — silent crashes or
hung processes are not permitted. Every log line MUST include the source file name and line number.
A `README.md` MUST document prerequisites (including Vulkan SDK installation), and
configure/build/run/test steps — a developer on a supported platform MUST be able to build and run
the engine from a clean clone by following only `README.md`.

**Rationale**: Engine initialization failures are the most common onboarding blocker. Clear error
messages with source locations eliminate guesswork and dramatically reduce setup time for new
contributors.

### III. Stability

All third-party dependencies acquired via CMake `FetchContent` MUST be pinned to an exact version
using `GIT_TAG` (no branch names, no `HEAD`). The Vulkan SDK MUST be acquired via
`find_package(Vulkan REQUIRED)` — it is never fetched via FetchContent. A `vendor/` directory is
reserved for any dependency that cannot be acquired via FetchContent; its use MUST be documented.
Every `CMakeLists.txt` MUST declare `cmake_minimum_required` explicitly. Root CMake configuration
MUST set `CMAKE_CXX_STANDARD_REQUIRED ON` and `CMAKE_CXX_EXTENSIONS OFF`.

**Rationale**: Unpinned dependencies produce non-reproducible builds. A developer cloning the repo
six months later MUST get a functionally identical build. `CMAKE_CXX_EXTENSIONS OFF` prevents
compiler-specific GNU extensions from silently enabling non-portable code.

### IV. Predictability

All project targets MUST be compiled with `-Wall -Wextra -Wpedantic -Werror` on GCC/Clang and
`/W4 /WX` on MSVC. These flags MUST be applied via the shared `sonnet_set_compile_options()`
CMake function. This function MUST NOT be called on FetchContent targets. FetchContent headers
MUST be added with the `SYSTEM` keyword to prevent their warnings from triggering project errors.
Vulkan validation layers (`VK_LAYER_KHRONOS_validation`) MUST be enabled in Debug builds via
the `SONNET_ENABLE_VALIDATION_LAYERS` CMake option (defaults `ON` for Debug, `OFF` for Release).
The SDL3 callback API (`SDL_MAIN_USE_CALLBACKS`) MUST be used as the engine entry point to
provide a deterministic, SDL-managed `AppInit → AppIterate → AppEvent → AppQuit` lifecycle.

**Rationale**: `-Werror` ensures latent bugs surface at compile time rather than at runtime.
Validation layers catch Vulkan API misuse during development before it manifests as GPU hangs or
driver crashes in release builds.

### V. Testability

Every module that exposes a public interface MUST have a corresponding abstract C++ interface
class with the `I` prefix (e.g., `IWindow`, `IRenderer`, `IRendererBackend`). All unit and
integration tests MUST use mock implementations of these interfaces — no real display, GPU,
Vulkan instance, or SDL3 initialization is ever required to run the test suite. Each module MUST
have its own Catch2 test executable registered with `catch_discover_tests()`. Integration tests
MUST exercise the full init/render/shutdown lifecycle using mock implementations only.
`ctest --output-on-failure` MUST pass on a headless machine without a GPU.

**Rationale**: If tests require a physical display or GPU, they cannot run in CI, blocking
automated quality gates. Abstract interfaces also enforce the Modularity principle: a module that
cannot be tested in isolation without its dependencies signals a design violation.

### VI. Portability

SDL3 and Vulkan MUST be used as the cross-platform windowing and graphics stack. Platform-specific
surface creation (Xlib, Win32, Metal, etc.) MUST be isolated inside SDL3 adapter code and MUST NOT
appear in any module above `SonnetWindow`. CMake MUST provide cross-platform build scripts
compatible with Linux, macOS, and Windows. The required Vulkan SDK version and installation
instructions MUST be documented in both `README.md` and `quickstart.md`.

**Rationale**: Portability constraints are cheapest to enforce at project inception. SDL3 already
abstracts platform-specific quirks including `WinMain`, `UIApplicationMain`, and Vulkan surface
extension selection — leaning on it fully avoids duplicating that work.

### VII. Internationalization

Hardcoded user-visible strings are permitted as tracked exceptions. Every hardcoded string MUST be
recorded in the `Complexity Tracking` table of the relevant `plan.md` with: (a) the specific
principle violated, (b) the rationale for why a locale system is not warranted at this stage, and
(c) a forward reference to when it will be addressed. A dedicated localization feature MUST be
planned once the engine has a meaningful surface of user-visible strings.

**Rationale**: A full locale subsystem is disproportionate for two developer-facing strings in a
greenfield engine. The exception model preserves the principle's intent (deliberate tracking of
technical debt) without imposing premature complexity on early features.

## Quality Standards

All code changes MUST pass the full test suite (`ctest --output-on-failure`) before merging to the
main branch. The following performance baselines apply to every rendering feature and MUST be
referenced as measurable success criteria in `spec.md`:

- **Frame rate**: ≥60 FPS stable for ≥60 seconds under normal operation
- **Memory**: ≤100 MB RAM; ≤100 MB VRAM
- **CPU**: ≤5% steady-state CPU usage; peaks permitted during the first 3 seconds of startup
- **Shutdown**: clean process exit within 1 second of the OS close event or SIGTERM
- **Leaks**: zero memory leaks on exit (verified via sanitizers or equivalent)

Performance regressions against these baselines MUST be approved and documented before merging.
Security vulnerabilities of severity MEDIUM or above MUST be resolved before any release.

## Development Workflow

Feature work MUST proceed through the Spec Kit workflow:
specification → clarification → planning → task generation → implementation.

**Mandatory stack for every feature**:
- **Language**: C++23 (`-std=c++23`, `CMAKE_CXX_STANDARD 23`)
- **Build system**: CMake 4.2.1 minimum
- **Logging**: spdlog wrapped exclusively behind `SONNET_LOG_*` macros; no spdlog types in public
  headers; `sonnet::logging::init()` MUST be the first call in the app entry point
- **Testing**: Catch2 v3.14.0 with `Catch2::Catch2WithMain` and `catch_discover_tests()`

Architectural decisions that affect two or more principles MUST be documented in `research.md`
under a named section with a Decision, Rationale, and Alternatives Considered. Code review MUST
include an explicit Constitution Check table in `plan.md` confirming compliance with all Core
Principles. Non-compliant code MUST NOT be merged without a documented and approved exception in
the Complexity Tracking table of the relevant `plan.md`.

## Governance

This constitution supersedes all prior conventions and informal agreements. Amendments require:
(1) a written proposal documenting the change and rationale, (2) review by at least one
maintainer, and (3) an update to this file with a version increment following semantic versioning
rules (MAJOR: principle removal or redefinition; MINOR: new principle or material expansion;
PATCH: clarification or wording). MAJOR version bumps require a migration plan covering in-flight
features. All pull requests MUST verify compliance with the Core Principles via the Constitution
Check gate in `plan.md`. Any complexity justified as an exception MUST reference the specific
principle violated and explain why the simpler alternative was rejected.

**Version**: 2.0.0 | **Ratified**: 2026-05-01 | **Last Amended**: 2026-05-01
