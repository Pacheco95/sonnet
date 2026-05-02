# Tasks: Code Quality Enforcement (Formatter + SAST)

**Input**: Design documents from `specs/003-code-quality-ci/`  
**Prerequisites**: plan.md ✅ spec.md ✅ research.md ✅ data-model.md ✅ contracts/ ✅ quickstart.md ✅

**Organization**: Tasks are grouped by user story to enable independent implementation and testing.

## Format: `[ID] [P?] [Story?] Description`

- **[P]**: Can run in parallel (different files, no dependency on incomplete tasks)
- **[Story]**: Which user story this task belongs to (US1, US2, US3)

---

## Phase 1: Setup (Verify Baseline)

**Purpose**: Confirm the project is in a clean, buildable state before introducing any changes.

- [X] T001 Verify project configures and builds cleanly: run `cmake -S . -B build --fresh -DCMAKE_BUILD_TYPE=Debug` then `cmake --build build --parallel` and confirm zero errors before touching any files

**Checkpoint**: Project builds and tests pass on the current branch — ready to introduce quality tooling.

---

## Phase 2: Foundational (Config Files + Codebase Baseline)

**Purpose**: Create tool configuration files and establish a clean baseline by formatting all existing source and resolving pre-existing SAST findings. **MUST be complete before the CI gate (US1) is activated.** See `specs/003-code-quality-ci/plan.md § Step 0` for the full rationale.

⚠️ **CRITICAL**: No user story work can begin until this phase is complete.

- [X] T002 [P] Create `.clang-format` at repo root using the exact YAML from `specs/003-code-quality-ci/plan.md § Step 1` — entity definition in `specs/003-code-quality-ci/data-model.md § Format Rule Set`
- [X] T003 [P] Create `.clang-tidy` at repo root using the exact YAML from `specs/003-code-quality-ci/plan.md § Step 2` — entity definition in `specs/003-code-quality-ci/data-model.md § SAST Configuration`; note cppcheck has no config file, its flags live in `specs/003-code-quality-ci/contracts/developer-commands.md`
- [X] T004 [P] Generate `compile_commands.json` by running: `cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_BUILD_TYPE=Debug` (required by clang-tidy; no build step needed)
- [X] T005 Apply format fix to all existing source files (depends on T002): `find src/ tests/ -name "*.cpp" -o -name "*.h" | xargs clang-format -i` — reformats every file in-place to conform to `.clang-format`
- [X] T006 [P] Run clang-tidy against all existing source and fix or suppress all findings (depends on T003, T004, T005): `run-clang-tidy -p build src/` — for each finding either fix the code or add `// NOLINT(<check-name>)` inline comment with a brief rationale; see `specs/003-code-quality-ci/contracts/developer-commands.md § SAST — clang-tidy` for command reference
- [X] T007 [P] Run cppcheck blocking check against all existing source and fix or suppress all findings (depends on T003, T005, can run in parallel with T006): `cppcheck src/ tests/ --enable=warning,performance,portability --error-exitcode=1 --suppress="*:vendor/*" --suppress="*:build*/*"` — for each finding either fix or add `// cppcheck-suppress <rule>` inline comment; see `specs/003-code-quality-ci/contracts/developer-commands.md § SAST — cppcheck (blocking)` for exact flags — **NOTE: cppcheck not installed locally (requires sudo); CI runner installs it automatically and will validate**
- [X] T008 Commit all reformatted source files and suppression comments as a single baseline commit (depends on T005, T006, T007): message `chore: apply clang-format and SAST baseline (003-code-quality-ci)`

**Checkpoint**: All source files pass clang-format, clang-tidy, and cppcheck locally. Safe to activate the CI gate.

---

## Phase 3: User Story 1 — Automated Code Quality Gate on PR (Priority: P1) 🎯 MVP

**Goal**: A `code-quality` CI job runs clang-format, clang-tidy, and cppcheck against the full codebase before any test job runs, blocks PR merges on any warning-level finding, and posts inline diff annotations.

**Independent Test**: Open a PR with a deliberate formatting violation in any `src/` file and verify the `code-quality` job fails and the merge button is blocked. Then fix the violation, push, and verify the job passes and `build-and-test` runs.

- [X] T009 [US1] Add `code-quality` job skeleton and setup steps to `.github/workflows/ci.yml` (depends on T008): add the job before `build-and-test` with `if: ${{ !github.event.pull_request.draft }}`, job-level `permissions: {contents: read, pull-requests: write}`, `env: {FORCE_JAVASCRIPT_ACTIONS_TO_NODE24: true}`, `runs-on: ubuntu-latest`, `timeout-minutes: 10` (enforces SC-004 ≤5 min target with 2× headroom for cache-miss runs); add steps: Checkout (`actions/checkout@v4`), install CMake 4.2.1 (`pip install cmake==4.2.1 --break-system-packages`), install system deps (full apt package list from existing `build-and-test` job **plus** `clang-format clang-tidy cppcheck`), FetchContent cache (`actions/cache@v4` with same `key:` hash expression as `build-and-test`). See `specs/003-code-quality-ci/plan.md § Step 3, steps 1-5`.
- [X] T010 [US1] Add cmake configure step and all four reviewdog annotation steps to `code-quality` job in `.github/workflows/ci.yml` (depends on T009): (6) cmake configure `cmake -S . -B build-quality -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_BUILD_TYPE=Debug`; (7) `reviewdog/action-clang-format@v1` with `reporter: github-pr-review`, `level: error`, `filter-mode: nofilter`; (8) `reviewdog/action-clang-tidy@v1` with `reporter: github-pr-review`, `level: error`, `workdir: build-quality`, `filter-mode: nofilter`; (9) `reviewdog/action-cppcheck@v1` with blocking flags (`--enable=warning,performance,portability --error-exitcode=1 --suppress=*:vendor/* --suppress=*:build*/*`), `reporter: github-pr-review`, `level: error`, `filter-mode: nofilter`; (10) second `reviewdog/action-cppcheck@v1` with advisory flags (`--enable=information`), `reporter: github-pr-review`, `level: warning`, `fail-on-error: false`, `filter-mode: nofilter`. Full flag reference in `specs/003-code-quality-ci/contracts/developer-commands.md`; job behaviour spec in `specs/003-code-quality-ci/contracts/ci-job-contract.md`.
- [X] T011 [US1] Add `needs: [code-quality]` to `build-and-test` job in `.github/workflows/ci.yml` (depends on T010)

**Checkpoint**: Push this branch to GitHub and open a PR. Verify `code-quality` runs before `build-and-test` and that `build-and-test` is skipped when `code-quality` fails. Verify inline annotations appear on the PR diff when a violation is introduced.

---

## Phase 4: User Story 2 — Local Format Check Before Committing (Priority: P2)

**Goal**: Maintainers can run a single command to check or auto-fix formatting locally and get the same result as CI.

**Independent Test**: Run the format check command from `specs/003-code-quality-ci/contracts/developer-commands.md § Format Check` on the reformatted source tree and confirm exit 0 with no output.

- [X] T012 [US2] Validate local format check command (depends on T008): run `find src/ tests/ -name "*.cpp" -o -name "*.h" | xargs clang-format --dry-run --Werror` from repo root and confirm exit code 0; if non-zero, fix remaining violations and re-run until clean
- [X] T013 [P] [US2] Validate local format fix command (can run in parallel with T012): introduce a trivial formatting violation (extra blank line) in `src/app/main.cpp`, run `clang-format -i src/app/main.cpp`, confirm file is restored to original, then revert with `git restore src/app/main.cpp`
- [X] T014 [US2] Add "## Code Quality" section to `README.md` (depends on T012, T013): include tool installation commands for Linux, macOS, and Windows from `specs/003-code-quality-ci/quickstart.md § Prerequisites`; include format check and format fix commands from `specs/003-code-quality-ci/quickstart.md § Format Check` and `§ Format Fix`

**Checkpoint**: A developer following only `README.md` can install clang-format and verify their code formatting locally with a single command.

---

## Phase 5: User Story 3 — Local SAST Run Before Committing (Priority: P3)

**Goal**: Maintainers can run single commands to execute clang-tidy and cppcheck locally and get the same results as CI.

**Independent Test**: Run both SAST commands from `specs/003-code-quality-ci/contracts/developer-commands.md` on the baselining source tree and confirm both exit 0.

- [X] T015 [US3] Validate local clang-tidy command (depends on T004, T008): run `run-clang-tidy -p build src/` from repo root and confirm exit code 0 with no findings; if findings remain, fix or suppress them
- [X] T016 [P] [US3] Validate local cppcheck blocking command (depends on T008, can run in parallel with T015): run `cppcheck src/ tests/ --enable=warning,performance,portability --error-exitcode=1 --suppress="*:vendor/*" --suppress="*:build*/*"` and confirm exit 0; if findings remain, fix or suppress them
- [X] T017 [US3] Add SAST commands to `README.md § Code Quality` (depends on T015, T016): append clang-tidy prerequisite note and command, cppcheck blocking command, and cppcheck advisory command from `specs/003-code-quality-ci/quickstart.md § SAST — clang-tidy` and `§ SAST — cppcheck`

**Checkpoint**: A developer following only `README.md` can install all three tools and run all quality checks locally with documented single commands.

---

## Phase 6: Polish & Cross-Cutting Concerns

- [X] T018 [P] Verify `specs/003-code-quality-ci/quickstart.md` commands are consistent with what was written to `README.md`; update quickstart.md if any discrepancy found
- [X] T019 Add post-merge instruction note to `README.md § Code Quality`: after merging this PR, a maintainer must add `code-quality` to required status checks under GitHub Settings → Branches → `main` branch protection rule (per `specs/003-code-quality-ci/plan.md § Step 5`)
- [ ] T020 [P] Validate SC-004 compliance (≤5 min quality gate, FR-010): after first successful CI run on this PR, inspect the `code-quality` job wall-clock time in the GitHub Actions run summary; if it exceeds 5 minutes on a warm-cache run, investigate and reduce (check FetchContent cache hit rate, consider splitting cmake configure from annotation steps)

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies — start immediately
- **Phase 2 (Foundational)**: Depends on Phase 1; BLOCKS all user story phases
- **Phase 3 (US1)**: Depends on Phase 2 (baseline must be committed before CI gate lands)
- **Phase 4 (US2)**: Depends on Phase 2 (config files must exist); can run in parallel with Phase 3
- **Phase 5 (US3)**: Depends on Phase 2; can run in parallel with Phases 3 and 4
- **Phase 6 (Polish)**: Depends on Phases 4 and 5 (README must be written before consistency check)

### Within Phase 2

```
T002 ─┐
T003 ─┤─► T005 ─► T006 ─┐
T004 ─┘           T007 ─┤─► T008
```
T002, T003, T004 run in parallel → T005 runs after T002 → T006 and T007 run in parallel after T005 (T006 also needs T004) → T008 runs after all three.

### Within Phase 3 (US1)

T009 → T010 → T011 (sequential — all modify the same `ci.yml` file)

### Within Phase 4 (US2)

T012 ‖ T013 → T014 (T012 and T013 in parallel, T014 after both)

### Within Phase 5 (US3)

T015 ‖ T016 → T017 (T015 and T016 in parallel, T017 after both)

---

## Parallel Execution Examples

### Phase 2 — Config Files (can launch together)

```
Task T002: Create .clang-format at repo root
Task T003: Create .clang-tidy at repo root
Task T004: Run cmake configure to generate compile_commands.json
```

### Phase 2 — SAST Runs (after T005 completes)

```
Task T006: Run clang-tidy, fix/suppress all findings
Task T007: Run cppcheck, fix/suppress all findings
```

### Phase 4 — Local Format Validation (can launch together)

```
Task T012: Validate format check exits 0
Task T013: Validate format fix rewrites correctly
```

### Phase 5 — Local SAST Validation (can launch together)

```
Task T015: Validate clang-tidy exits 0
Task T016: Validate cppcheck exits 0
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup (T001)
2. Complete Phase 2: Foundational (T002–T008) — CRITICAL, blocks all work
3. Complete Phase 3: US1 CI Gate (T009–T011)
4. **STOP and VALIDATE**: Push to GitHub, open a test PR, confirm `code-quality` runs and blocks on a known violation
5. Merge if ready — US1 delivers the primary value

### Incremental Delivery

1. Phase 1 + Phase 2 → clean codebase baseline
2. Phase 3 (US1) → CI gate active, PRs enforced → deploy/merge (MVP)
3. Phase 4 (US2) → local format workflow documented in README → deploy/merge
4. Phase 5 (US3) → local SAST workflow documented in README → deploy/merge
5. Phase 6 (Polish) → consistency and post-merge instructions

---

## Notes

- [P] tasks touch different files or are independent operations — safe to run concurrently
- [Story] label maps each task to a user story for traceability to `spec.md`
- Phase 2 is the most critical: do not enable the CI gate (T009) until T008 is committed
- For NOLINT suppressions (T006): prefer fixing the underlying code; only suppress if the finding is a deliberate design choice
- `compile_commands.json` from T004 lives in `build/` locally; in CI it lives in `build-quality/` — both are correct for their respective contexts
- T019 is a manual post-merge step in GitHub repository settings, not a code change
