# Data Model: GitHub Actions CI Workflow Configuration

**Branch**: `002-github-actions-ci` | **Date**: 2026-05-01 | **Plan**: [plan.md](plan.md)

This document describes the configuration schema for the CI workflow and branch protection rule. These are the "entities" for a CI feature — structured configuration objects with defined fields, constraints, and relationships.

---

## Workflow File: `.github/workflows/ci.yml`

### Trigger Configuration

```
WorkflowTrigger
├── event: pull_request
│   ├── branches: [main]                        # Only PRs targeting main
│   └── types:                                  # Only these PR lifecycle events
│       ├── opened
│       ├── synchronize                         # New commits pushed to open PR
│       ├── reopened
│       └── ready_for_review                    # Draft converted to ready
```

**Constraint**: `push` events are NOT in the trigger list. CI runs only in the context of a PR, never on direct pushes to branches. This aligns with the merge gate intent.

---

### Job Definition

```
Job: build-and-test
├── runs-on: ubuntu-latest
├── timeout-minutes: 30                         # SC-003: hard ceiling; exceeded = failed check
├── condition: github.event.pull_request.draft == false   # FR-001: skip draft PRs
│
├── Steps (ordered):
│   1. checkout          # actions/checkout@v4
│   2. system-deps       # apt-get: Vulkan headers, glslc, SDL3 build deps
│   3. cmake-install     # pip install cmake==4.2.1
│   4. cache-restore     # actions/cache@v4 → build/_deps
│   5. configure         # cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
│   6. build             # cmake --build build --parallel
│   7. test              # ctest --test-dir build --output-on-failure
│   8. upload-artifacts  # actions/upload-artifact@v4 (always runs, even on failure)
│       ├── name: test-results
│       ├── path: build/Testing/
│       └── retention-days: 7
```

**Status check identifier**: `build-and-test`  
This name is the anchor for the branch protection rule. See `contracts/ci-workflow.md`.

---

### Cache Entity

```
DependencyCache
├── key: {runner.os}-cmake-deps-{hash(cmake/FetchDependencies.cmake, CMakeLists.txt)}
├── restore-keys: [{runner.os}-cmake-deps-]     # Partial match fallback
├── path: build/_deps                            # FetchContent download + build dir
│
├── Invalidation trigger: any change to FetchDependencies.cmake or CMakeLists.txt
└── Scope: per OS (cache is not shared between Linux/macOS/Windows if matrix added later)
```

---

## Branch Protection Rule

```
BranchProtectionRule (applied to: main)
├── required_status_checks:
│   ├── strict: true                            # Branch must be up-to-date before merging
│   └── contexts: [build-and-test]              # Must match Job.id exactly
├── enforce_admins: true                        # SC-002: no admin bypass
├── required_pull_request_reviews: (optional — not in scope of this feature)
├── allow_force_pushes: false                   # SC-005: prevent bypass via force push
├── allow_deletions: false
└── restrictions: null                          # No push restrictions needed
```

**Relationship**: `BranchProtectionRule.contexts` → `Job.id` (`build-and-test`). If the job is renamed, the protection rule must be updated — otherwise the gate silently disappears.

---

## State Transitions

```
PR State Machine (from CI perspective):

[draft]
  │  on: ready_for_review
  ▼
[open, pending]  ──────────────────────────────► [open, running]
                    CI triggered                      │
                                             build + test executes
                                                      │
                                      ┌───────────────┴───────────────┐
                                      ▼                               ▼
                               [open, failing]                 [open, passing]
                                      │                               │
                        push fix commits                         reviewer approves
                        (→ synchronize event)                         │
                               │                                      ▼
                        [open, running]                        [mergeable]
                               │                                      │
                       ...cycle repeats                        git merge → main
                                                                      │
                                                               [closed / merged]

Timeout (>30 min):  [open, running] → [open, failing]
Config error:       [open, running] → [open, errored]  (treated as failing for gate purposes)
```

---

## Artifact Schema

```
TestResultsArtifact
├── name: test-results
├── source_path: build/Testing/
│   └── contains: CTest XML output (LastTest.log, Test.xml per CDash format)
├── retention_days: 7
├── upload_condition: always()                  # Uploaded even when tests fail
└── access: GitHub Actions run summary → linked from PR checks tab
```
