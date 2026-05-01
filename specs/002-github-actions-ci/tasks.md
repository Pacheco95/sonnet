# Tasks: GitHub Actions CI with PR Test Gates

**Input**: Design documents from `/specs/002-github-actions-ci/`
**Prerequisites**: plan.md ✅ | spec.md ✅ | research.md ✅ | data-model.md ✅ | contracts/ ✅

**Organization**: Tasks grouped by user story — each story is independently completable.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies on incomplete tasks)
- **[Story]**: Which user story this task belongs to ([US1], [US2], [US3])

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Create the CI directory structure and confirm the local test baseline.

- [ ] T001 Create `.github/workflows/` directory at repository root
- [ ] T002 Verify local headless test suite passes: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build --parallel && ctest --test-dir build --output-on-failure` — confirm zero failures before writing CI

**Checkpoint**: Local test suite passes headlessly. CI directory exists.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Infrastructure that MUST be in place before any user story work.

**⚠️ CRITICAL**: No user story work can begin until this phase is complete.

- [ ] T003 Verify that `libvulkan-dev` and `glslc` packages satisfy `find_package(Vulkan REQUIRED)` and `Vulkan::glslc` on Ubuntu 24.04 by running `docker run --rm -v $(pwd):/src ubuntu:24.04 bash -c "apt-get update && apt-get install -y cmake libvulkan-dev glslc && cmake -S /src -B /tmp/test-build"` — document result in `specs/002-github-actions-ci/research.md` under "Ubuntu Package Verification"
- [ ] T004 [P] Identify and record the minimal set of Ubuntu apt packages required for SDL3 3.4.4 to compile from source via FetchContent on Ubuntu 24.04 — update `specs/002-github-actions-ci/research.md` under "SDL3 Build Dependencies"

**Checkpoint**: Exact apt package list confirmed. Ready to write `ci.yml`.

---

## Phase 3: User Story 1 - Automated Test Execution on PR (Priority: P1) 🎯 MVP

**Goal**: CI runs automatically on every non-draft PR targeting `main`, builds the project, runs `ctest`, and blocks merge on any failure.

**Independent Test**: Open a PR with a deliberately broken test. Confirm the `build-and-test` status check fails and the merge button is disabled. Fix the test and push. Confirm the check passes and merge is re-enabled.

### Implementation for User Story 1

- [ ] T005 [US1] Create `.github/workflows/ci.yml` with the workflow trigger block: `on: pull_request: branches: [main] types: [opened, synchronize, reopened, ready_for_review]`
- [ ] T006 [US1] Add `jobs: build-and-test:` definition to `.github/workflows/ci.yml` with `runs-on: ubuntu-latest`, `timeout-minutes: 30`, and `if: ${{ !github.event.pull_request.draft }}` condition
- [ ] T007 [US1] Add `actions/checkout@v4` step and `pip install cmake==4.2.1` step to `.github/workflows/ci.yml`
- [ ] T008 [US1] Add apt dependency installation step to `.github/workflows/ci.yml`: `libvulkan-dev glslc` plus SDL3 build packages confirmed in T004
- [ ] T009 [US1] Add `actions/cache@v4` step to `.github/workflows/ci.yml` caching `build/_deps` with key `${{ runner.os }}-cmake-deps-${{ hashFiles('cmake/FetchDependencies.cmake', 'CMakeLists.txt') }}`
- [ ] T010 [US1] Add CMake configure step to `.github/workflows/ci.yml`: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug`
- [ ] T011 [US1] Add build step to `.github/workflows/ci.yml`: `cmake --build build --parallel`
- [ ] T012 [US1] Add CTest step to `.github/workflows/ci.yml`: `ctest --test-dir build --output-on-failure`

**Checkpoint**: Push `.github/workflows/ci.yml` to a test branch and open a PR. Confirm the `build-and-test` check appears and runs to completion. US1 is fully functional.

---

## Phase 4: User Story 2 - Test Results Visible to Reviewers (Priority: P2)

**Goal**: Test logs and reports are accessible from the PR for 7 days; branch protection enforces the gate for all contributors including administrators.

**Independent Test**: Open a PR, let CI run, click the `build-and-test` Details link, and confirm the Artifacts section shows `test-results` available for download. Confirm that without a passing check the Merge button is greyed out for an admin account.

### Implementation for User Story 2

- [ ] T013 [US2] Add `actions/upload-artifact@v4` step to `.github/workflows/ci.yml` with `if: always()`, `name: test-results`, `path: build/Testing/`, `retention-days: 7` (append after T012; edits same file as T005–T012, cannot run in parallel)
- [ ] T014 [US2] Configure the GitHub branch protection rule on `main` via repository Settings → Branches: enable "Require status checks to pass" with context `build-and-test`, enable "Require branches to be up to date", enable "Do not allow bypassing the above settings" (include administrators), disable force pushes, disable deletions

**Checkpoint**: Reviewer opens a PR and sees the check status and artifact link without any extra navigation. Merge button is blocked for all roles until check passes. US2 complete.

---

## Phase 5: User Story 3 - Re-run Tests After Fix (Priority: P3)

**Goal**: Pushing new commits to an open PR with a failing check automatically re-triggers CI; on success the merge gate lifts without any manual action.

**Independent Test**: With a PR that has a failing `build-and-test` check and blocked merge: push a commit that fixes the failure. Confirm a new CI run starts automatically and, upon success, the merge gate lifts.

### Implementation for User Story 3

- [ ] T015 [US3] Verify the `synchronize` event in `.github/workflows/ci.yml` triggers correctly: push a new commit to an open PR with a previously failing check and confirm a new run starts automatically (this event is already declared in T005; task is acceptance-level validation)
- [ ] T016 [US3] Perform full end-to-end smoke test: (1) open a PR with a deliberate test failure, (2) confirm merge is blocked, (3) push a fix commit, (4) confirm CI re-runs and passes, (5) confirm merge is re-enabled — document result as acceptance evidence in `specs/002-github-actions-ci/quickstart.md`

**Checkpoint**: Full PR lifecycle validated end-to-end. All three user stories complete.

---

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**: Documentation and discoverability improvements across the feature.

- [ ] T017 [P] Add a GitHub Actions CI status badge to `README.md` using the absolute URL: `[![CI](https://github.com/Pacheco95/sonnet/actions/workflows/ci.yml/badge.svg)](https://github.com/Pacheco95/sonnet/actions/workflows/ci.yml)`
- [ ] T018 [P] Update `specs/002-github-actions-ci/quickstart.md` with the acceptance evidence captured in T016 (confirm all steps match the real workflow behavior)
- [ ] T019 Verify the dependency cache is working: compare CI run time of a cold run (first push) vs. a warm run (second push with no dep changes) — confirm warm run is measurably faster (SC-007)

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies — start immediately
- **Phase 2 (Foundational)**: Depends on Phase 1 — BLOCKS all user stories
- **Phase 3 (US1)**: Depends on Phase 2 — MVP deliverable
- **Phase 4 (US2)**: Depends on Phase 3 (must have a working workflow before configuring branch protection)
- **Phase 5 (US3)**: Depends on Phase 4 (branch protection must be active to test the gate-lift behavior)
- **Phase 6 (Polish)**: Depends on Phase 5

### User Story Dependencies

- **US1 (P1)**: Depends only on Foundational phase — no dependency on US2 or US3
- **US2 (P2)**: Artifact upload (T013) can be added to `ci.yml` in parallel with US1 work; branch protection (T014) must wait for US1 to produce a working check name
- **US3 (P3)**: Validation only — `synchronize` trigger is declared in T005 (US1); US3 depends on US1 and US2 being active

### Within Each User Story

- T005 → T006 → T007 → T008 → T009 → T010 → T011 → T012 (sequential — all steps in same file)
- T013 must follow T012 (appends to the same `ci.yml` file; cannot run in parallel)
- T014 must follow T005 (branch protection needs the exact job name from `ci.yml`)

---

## Parallel Opportunities

```
# Phase 2 — run in parallel:
Task: T003  "Verify libvulkan-dev / glslc on Ubuntu 24.04"
Task: T004  "Identify SDL3 build deps"

# Note: T013 must follow T012 (same ci.yml file — no parallel opportunity)
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup — confirm local tests pass
2. Complete Phase 2: Foundational — confirm apt packages
3. Complete Phase 3: US1 — write `ci.yml`, verify CI runs
4. **STOP and VALIDATE**: CI is running; merge gate is not yet enforced (branch protection not configured)
5. Proceed to US2 only after US1 is confirmed working

### Incremental Delivery

1. Phase 1–2 → Local test baseline confirmed, `ci.yml` directory ready
2. Phase 3 (US1) → CI running on PRs, check visible ← MVP: contributors can see results
3. Phase 4 (US2) → Artifacts accessible + merge gate enforced ← Full feature: no bypass possible
4. Phase 5 (US3) → End-to-end smoke test validates re-run behavior
5. Phase 6 → Badge, docs polish

---

## Notes

- [P] tasks involve different files or non-overlapping sections — safe to run in parallel
- The job name `build-and-test` in `ci.yml` is a stable contract — see `contracts/ci-workflow.md` before renaming
- Branch protection (T014) requires repository admin permissions
- All tests run headless without a GPU per Constitution Principle V — no special CI hardware needed
- Commit after each phase checkpoint to maintain a clean history
