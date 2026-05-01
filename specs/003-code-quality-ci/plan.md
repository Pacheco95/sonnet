# Implementation Plan: Code Quality Enforcement (Formatter + SAST)

**Branch**: `003-code-quality-ci` | **Date**: 2026-05-01 | **Spec**: [spec.md](spec.md)  
**Input**: Feature specification from `specs/003-code-quality-ci/spec.md`

## Summary

Add a `code-quality` GitHub Actions job that runs clang-format (formatting) and clang-tidy + cppcheck (SAST) against the entire codebase as the first CI stage, blocking PR merges on any warning-level or above finding and surfacing informational findings as non-blocking annotations. Maintainers can run all three tools locally with single documented commands using the same configuration files used in CI.

## Technical Context

**Language/Version**: C++23 (`-std=c++23`, `CMAKE_CXX_STANDARD 23`)  
**Primary Dependencies**: CMake 4.2.1, clang-format 14+, clang-tidy 14+, cppcheck 2.10+, reviewdog (CI only)  
**Storage**: N/A  
**Testing**: Catch2 v3.14.0 (existing; no changes in this feature)  
**Target Platform**: Linux (CI on `ubuntu-latest`); tools also available on macOS and Windows  
**Project Type**: Desktop game engine (C++/CMake)  
**Performance Goals**: code-quality CI stage completes in ≤5 minutes (SC-004)  
**Constraints**: FOSS tools only; no CI auto-commit; entire codebase scanned per run  
**Scale/Scope**: ~5 source modules under `src/`; `tests/` directory

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-checked after Phase 1 design.*

| Principle | Status | Notes |
|-----------|--------|-------|
| I. Modularity | ✅ Pass | No new engine modules introduced; CI tooling only |
| II. Usability | ✅ Pass | README and quickstart.md MUST document tool installation and local commands (see Phase 1) |
| III. Stability | ✅ Pass | All GitHub Actions action versions pinned to exact tags; no new FetchContent dependencies |
| IV. Predictability | ✅ Pass | `.clang-format` and `.clang-tidy` configs versioned in repo; same config used locally and in CI |
| V. Testability | ✅ Pass | No changes to engine interfaces or test infrastructure |
| VI. Portability | ✅ Pass | Tool installation documented for Linux, macOS, Windows in quickstart.md |
| VII. Internationalization | ✅ Pass | No user-visible strings introduced |

**Gate result**: No violations. Proceeding to Phase 1.

## Project Structure

### Documentation (this feature)

```text
specs/003-code-quality-ci/
├── plan.md               # This file
├── research.md           # Tool selection decisions
├── data-model.md         # Config entity definitions
├── quickstart.md         # Local tool setup guide
├── contracts/
│   ├── developer-commands.md   # Local command interface
│   └── ci-job-contract.md      # CI job behaviour contract
└── tasks.md              # Phase 2 output (/speckit-tasks command)
```

### Source Code (repository root)

```text
.clang-format             # Format rule set (new)
.clang-tidy               # SAST configuration (new)

.github/
└── workflows/
    └── ci.yml            # Modified: add code-quality job before build-and-test

README.md                 # Modified: add "Code Quality" section with local commands
```

**Structure Decision**: Single-project layout. All changes are at the repo root or inside `.github/workflows/`. No new subdirectories in `src/`. The existing `ci.yml` is modified in-place to add the `code-quality` job.

## Phase 0: Research

See [research.md](research.md) — all tool selection decisions resolved.

**Key decisions**:
- **Formatter**: clang-format with LLVM base style, 100-column limit, C++23 awareness
- **SAST**: clang-tidy (semantic/AST) + cppcheck (pattern), complementary coverage
- **CI annotations**: reviewdog `github-pr-review` reporter for inline diff comments
- **Compile DB**: `cmake -S . -B build-quality -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` (configure only, no build)
- **Vendor exclusion**: path filters on `src/`/`tests/` for clang-tidy; `--suppress` flags for cppcheck

## Phase 1: Design & Contracts

### Step 0 — Baseline: apply format fix and resolve pre-existing SAST findings

**This step must be completed before the CI gate is activated (before Step 3 is merged).** Adding `.clang-format` and `.clang-tidy` to a codebase that was not previously formatted under those rules will cause the CI to fail immediately on all existing code.

1. Write `.clang-format` (Step 1) and `.clang-tidy` (Step 2) first, without touching `ci.yml`.
2. Run the format fix command (see [`contracts/developer-commands.md § Format Fix`](contracts/developer-commands.md)) against all existing source files: `find src/ tests/ -name "*.cpp" -o -name "*.h" | xargs clang-format -i`
3. Run the cmake configure step to produce `compile_commands.json`, then run clang-tidy locally (see [`contracts/developer-commands.md § SAST — clang-tidy`](contracts/developer-commands.md)). Fix any findings, or add targeted per-line suppressions (`// NOLINT(<check-name>)`) for any findings that are deliberate and safe.
4. Run cppcheck locally (see [`contracts/developer-commands.md § SAST — cppcheck (blocking)`](contracts/developer-commands.md)). Fix or add `cppcheck-suppress` inline comments for any pre-existing false positives.
5. Commit the reformatted source files and any suppression comments as a single "chore: apply clang-format baseline and resolve pre-existing SAST findings" commit.
6. Only after this baseline commit is on the branch should `ci.yml` be modified in Step 3.

### Step 1 — `.clang-format` configuration

Entity definition: [`data-model.md § Format Rule Set`](data-model.md)

File: `.clang-format` (repo root)

```yaml
BasedOnStyle: LLVM
Standard: c++23
ColumnLimit: 100
IndentWidth: 4
AccessModifierOffset: -4
AlignAfterOpenBracket: Align
AlwaysBreakTemplateDeclarations: Yes
PointerAlignment: Left
SpaceBeforeParens: ControlStatements
BreakBeforeBraces: Attach
```

Scope: `*.cpp`, `*.h` under `src/` and `tests/`. Excludes `vendor/`, `build*/`, `cmake/`, `.github/`.

### Step 2 — `.clang-tidy` configuration

Entity definition: [`data-model.md § SAST Configuration`](data-model.md)

Note: cppcheck has no config file — its flags are embedded in the CI step and local commands. See [`data-model.md § SAST Configuration — cppcheck flags`](data-model.md) and [`contracts/developer-commands.md`](contracts/developer-commands.md) for the authoritative flag set.

File: `.clang-tidy` (repo root)

```yaml
Checks: >-
  bugprone-*,
  clang-analyzer-*,
  modernize-*,
  performance-*,
  readability-*,
  -readability-identifier-naming,
  -modernize-use-trailing-return-type
WarningsAsErrors: "*"
HeaderFilterRegex: "^((?!/_deps/|/vendor/).)*$"
FormatStyle: file
```

Disabled checks rationale:
- `readability-identifier-naming`: would require naming convention enforcement as a prerequisite to enable; deferred
- `modernize-use-trailing-return-type`: stylistic preference, not a quality gate

### Step 3 — `ci.yml` modification

Add a `code-quality` job **before** `build-and-test`. Make `build-and-test` declare `needs: [code-quality]`.

Full behavioural spec for this job: [`contracts/ci-job-contract.md`](contracts/ci-job-contract.md).

**`permissions:` block**: reviewdog requires `pull-requests: write` to post review comments. Declare this at job level (not workflow level) to avoid widening permissions on `build-and-test`. The existing workflow has no top-level `permissions:` block; adding one there would change the effective defaults for all jobs.

```yaml
jobs:
  code-quality:
    permissions:
      contents: read
      pull-requests: write
```

**`env:` block**: carry over the same env var used by `build-and-test` to suppress Node.js deprecation warnings from Actions runners:

```yaml
    env:
      FORCE_JAVASCRIPT_ACTIONS_TO_NODE24: true
```

**code-quality job steps**:

1. Checkout (`actions/checkout@v4` — same tag as `build-and-test`)
2. Install CMake 4.2.1 (same `pip install cmake==4.2.1` command as `build-and-test`)
3. Install system dependencies — same `apt-get` package list as `build-and-test` (Vulkan headers + SDL3 build deps are required so `find_package(Vulkan)` and FetchContent SDL3 succeed during cmake configure)
4. Install quality tools: `clang-format clang-tidy cppcheck` appended to the same `apt-get install` call in step 3
5. Cache CMake dependencies (`actions/cache@v4`, same `key:` expression as `build-and-test` so both jobs share the FetchContent cache)
6. Configure CMake (configure only — no build): `cmake -S . -B build-quality -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_BUILD_TYPE=Debug`
7. Run reviewdog clang-format (`reviewdog/action-clang-format@v1`): `reporter: github-pr-review`, `level: error`, `filter-mode: nofilter` (scan entire repo, not just diff). See [`contracts/developer-commands.md § Format Check`](contracts/developer-commands.md) for equivalent local command.
8. Run reviewdog clang-tidy (`reviewdog/action-clang-tidy@v1`): `reporter: github-pr-review`, `level: error`, `workdir: build-quality` (points the action at `build-quality/compile_commands.json`), `filter-mode: nofilter`. See [`contracts/developer-commands.md § SAST — clang-tidy`](contracts/developer-commands.md) for equivalent local command.
9. Run reviewdog cppcheck — blocking (`reviewdog/action-cppcheck@v1`): flags `--enable=warning,performance,portability --error-exitcode=1 --suppress=*:vendor/* --suppress=*:build*/*`, `reporter: github-pr-review`, `level: error`, `filter-mode: nofilter`. See [`contracts/developer-commands.md § SAST — cppcheck (blocking)`](contracts/developer-commands.md) for exact flags.
10. Run reviewdog cppcheck — advisory (`reviewdog/action-cppcheck@v1`): flags `--enable=information --suppress=*:vendor/* --suppress=*:build*/*`, `reporter: github-pr-review`, `level: warning`, `fail-on-error: false`, `filter-mode: nofilter`. See [`contracts/developer-commands.md § SAST — cppcheck (informational)`](contracts/developer-commands.md) for exact flags.

**Action version note**: `@v1` follows the same floating semver-tag convention used by the existing workflow (`actions/checkout@v4`, `actions/cache@v4`). If stricter pinning is later required, replace with a specific patch tag (e.g. `reviewdog/action-clang-format@v1.19.0`).

**build-and-test change**: Add `needs: [code-quality]` at the job level so it is skipped when the quality gate fails or is cancelled.

### Step 4 — README.md update

Reference: [`quickstart.md`](quickstart.md) contains the full install and command reference to copy from.

Add a "## Code Quality" section documenting:
- Tools installed (`clang-format`, `clang-tidy`, `cppcheck`)
- One-line install commands per platform
- The four local commands from `contracts/developer-commands.md`

Constitution II (Usability) requires that a developer can follow only `README.md` to build and run the engine — this extends to the quality tools.

### Step 5 — Branch protection

After merging, the `code-quality` required status check must be added to the `main` branch protection rule in GitHub repository settings. This is a post-merge manual step (repository settings, not in-code).

## Complexity Tracking

No Constitution violations requiring justification.
