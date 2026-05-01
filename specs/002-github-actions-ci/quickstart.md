# Quickstart: GitHub Actions CI with PR Test Gates

**Date**: 2026-05-01 | **Plan**: [plan.md](plan.md)

---

## Overview

Every non-draft pull request targeting `main` automatically runs the Sonnet build and test suite in CI. The PR cannot be merged until this check passes. This guide explains what contributors and maintainers need to know.

---

## For Contributors: Opening a PR

1. Push your branch and open a pull request against `main`.
2. If the PR is a **draft**, CI does not run. Convert it to "Ready for review" when you want CI to trigger.
3. Once ready for review, the `build-and-test` check appears in the **Checks** tab of the PR within seconds.
4. CI runs the full Sonnet build + test suite. Results appear within 30 minutes (typically much faster with cached dependencies).

### Reading the results

| Check status | Meaning |
|---|---|
| 🟡 Pending | CI is queued or running |
| ✅ Passing | All tests passed — PR is eligible for merge |
| ❌ Failing | One or more tests failed, the build failed, or a timeout occurred |

Click **Details** next to the `build-and-test` check to open the Actions run log. Expand the **Test** step to see individual test output.

### Downloading test artifacts

Test reports are retained for **7 days** and available from the Actions run summary:

1. Click **Details** on the failing check
2. Scroll to the **Artifacts** section of the run summary
3. Download `test-results` — contains CTest XML output with per-test pass/fail details

---

## For Contributors: Fixing a Failing Check

1. Diagnose the failure from the **Details** log or the downloaded `test-results` artifact.
2. Fix the issue locally. Verify with:
   ```bash
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
   cmake --build build --parallel
   ctest --test-dir build --output-on-failure
   ```
3. Push your fix to the PR branch. CI triggers automatically on the new commit.
4. Once all tests pass, the merge gate lifts automatically — no manual action needed.

---

## For Maintainers: Initial Branch Protection Setup

After merging this feature's PR, configure branch protection for `main` in GitHub:

1. Go to **Settings → Branches → Add branch protection rule**
2. Set **Branch name pattern**: `main`
3. Enable the following:

   | Setting | Value |
   |---|---|
   | Require a pull request before merging | ✅ |
   | Require status checks to pass before merging | ✅ |
   | Status check | `build-and-test` ← exact name required |
   | Require branches to be up to date before merging | ✅ |
   | Do not allow bypassing the above settings | ✅ (enforces admins too) |
   | Allow force pushes | ❌ |
   | Allow deletions | ❌ |

4. Click **Save changes**.

> The status check name `build-and-test` must match the job key in `.github/workflows/ci.yml` exactly. See [`contracts/ci-workflow.md`](contracts/ci-workflow.md) for the change protocol if the job is ever renamed.

---

## For Maintainers: Verifying the Gate Works

1. Open a test PR with a deliberately failing test (or a compilation error).
2. Confirm the `build-and-test` check fails.
3. Confirm the **Merge** button is greyed out.
4. Fix the PR and confirm the check passes and the button re-enables.

---

## CI Environment Summary

| Property | Value |
|---|---|
| Runner | `ubuntu-latest` (Ubuntu 24.04) |
| CMake | 4.2.1 |
| Compiler | GCC 13 (C++23) |
| Build type | Debug |
| Timeout | 30 minutes |
| Artifact retention | 7 days |
| Vulkan | Headers from `libvulkan-dev` (headless — no GPU required) |
| Dependencies | Cached via `build/_deps` (GitHub Actions cache) |

All tests run headless without a GPU or display server. The test suite uses mock implementations of `IWindow` and `IRenderer` per the project's testability architecture.
