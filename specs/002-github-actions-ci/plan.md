# Implementation Plan: GitHub Actions CI with PR Test Gates

**Branch**: `002-github-actions-ci` | **Date**: 2026-05-01 | **Spec**: [spec.md](spec.md)  
**Input**: Feature specification from `/specs/002-github-actions-ci/spec.md`

## Summary

Add a GitHub Actions workflow that builds the Sonnet C++23/CMake project and runs the full Catch2/CTest test suite on every non-draft pull request targeting `main`. A GitHub branch protection rule enforces that this check must pass before any PR can be merged. Tests run headless (no GPU or display) using mock implementations per Constitution Principle V, making the CI fully self-contained on a standard Ubuntu runner.

## Technical Context

**Language/Version**: C++23 (`-std=c++23`, `CMAKE_CXX_STANDARD_REQUIRED ON`)  
**Primary Dependencies**:
- GitHub Actions runner: `ubuntu-latest` (Ubuntu 24.04)
- Vulkan headers: `libvulkan-dev` (system package — headers + loader for compilation)
- `glslc`: shader compiler (system package — for SPIR-V compilation in the build)
- SDL3 3.4.4, spdlog v1.17.0, Catch2 v3.14.0, Vulkan-Hpp v1.4.341 — all via FetchContent (unchanged)
- `actions/checkout@v4`, `actions/cache@v4`, `actions/upload-artifact@v4` — pinned GitHub Actions

**Storage**: `build/_deps` cached in GitHub Actions cache (keyed on `cmake/FetchDependencies.cmake` + `CMakeLists.txt` hashes)  
**Testing**: `ctest --test-dir build --output-on-failure` — headless, no GPU/display required  
**Target Platform**: `ubuntu-latest` CI runner (Linux); macOS/Windows matrix deferred to future feature  
**Project Type**: CI/CD pipeline configuration (new `.github/workflows/ci.yml` + branch protection rule)  
**Performance Goals**: CI run completes within 30-minute timeout; dependency cache reduces cold build time  
**Constraints**: Tests must pass without GPU or display (Constitution V); all GitHub Actions pinned to exact version tags (Constitution III); no bypass for repository administrators (spec FR-006, SC-002)  
**Scale/Scope**: Single workflow file, single protected branch (`main`), single required status check

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-checked after Phase 1 design.*

| Principle | Status | Notes |
|-----------|--------|-------|
| I. Modularity | ✅ PASS | CI introduces no new C++ modules and no new CMake targets. Workflow validates the existing module architecture without coupling anything. |
| II. Usability | ✅ PASS | CI mirrors the `README.md` build and test steps exactly. `--output-on-failure` surfaces failing test output inline on the PR. |
| III. Stability | ✅ PASS | All GitHub Actions pinned to SHA-anchored version tags (`@v4`). FetchContent deps remain pinned by `GIT_TAG` as declared in `cmake/FetchDependencies.cmake`; no version changes introduced. |
| IV. Predictability | ✅ PASS | Build uses `Debug` configuration, which activates `sonnet_set_compile_options()` (`-Wall -Wextra -Wpedantic -Werror`) and Vulkan validation layers. CI will fail on any new warning. |
| V. Testability | ✅ PASS | All tests are mock-based and headless by design — `ctest` passes on a standard Ubuntu runner with no GPU. This is the foundational requirement that makes CI possible. |
| VI. Portability | ✅ PASS | Linux runner as primary target. The workflow structure (single `build-and-test` job) is easily extended to a matrix for macOS and Windows in a future feature. |
| VII. Internationalization | ✅ N/A | CI workflow configuration contains no user-visible strings. |

## Project Structure

### Documentation (this feature)

```text
specs/002-github-actions-ci/
├── plan.md              # This file
├── research.md          # Phase 0 findings
├── data-model.md        # Workflow configuration schema
├── quickstart.md        # CI observer guide for contributors
├── contracts/
│   └── ci-workflow.md   # Status check name contract (branch protection anchor)
└── tasks.md             # Phase 2 output (/speckit-tasks — not created here)
```

### Source Code (repository root)

```text
.github/
└── workflows/
    └── ci.yml           # New: GitHub Actions CI workflow

# All existing source files unchanged — this feature is purely CI infrastructure
src/
tests/
cmake/
CMakeLists.txt
```

**Structure Decision**: Single new file at `.github/workflows/ci.yml`. No changes to source, tests, or CMake configuration. Branch protection rule configured via GitHub repository settings (documented in `quickstart.md`).
