# Contract: CI Workflow Status Check

**Branch**: `002-github-actions-ci` | **Date**: 2026-05-01 | **Plan**: [../plan.md](../plan.md)

---

## Purpose

This contract documents the **status check identifier** that GitHub branch protection rules reference. The identifier is derived from the GitHub Actions job name and must remain stable. Renaming the job without updating the branch protection rule silently removes the merge gate.

---

## Status Check Identifier

```
build-and-test
```

This string must appear verbatim in:
1. The `jobs:` key of `.github/workflows/ci.yml`
2. The `required_status_checks.contexts` list of the branch protection rule on `main`

---

## Workflow File Contract

```yaml
# .github/workflows/ci.yml — required structure

name: CI

on:
  pull_request:
    branches: [main]
    types: [opened, synchronize, reopened, ready_for_review]

jobs:
  build-and-test:          # ← THIS NAME IS THE STATUS CHECK IDENTIFIER
    if: ${{ !github.event.pull_request.draft }}
    runs-on: ubuntu-latest
    timeout-minutes: 30
    # ... steps ...
```

**Stability rule**: The `jobs.build-and-test` key MUST NOT be renamed without a simultaneous update to the repository's branch protection rule. Treat it as a public API.

---

## Branch Protection Rule Contract

The following settings MUST be applied to the `main` branch via GitHub repository settings (`Settings → Branches → Branch protection rules`):

| Setting | Required Value | Reason |
|---------|---------------|--------|
| Require status checks to pass | ✅ enabled | Core merge gate |
| Status check name | `build-and-test` | Must match job key exactly |
| Require branches to be up to date | ✅ enabled | Prevents stale-base merges |
| Include administrators | ✅ enabled | SC-002: no bypass |
| Allow force pushes | ❌ disabled | SC-005: prevent gate bypass |
| Allow deletions | ❌ disabled | Protect branch integrity |

---

## Change Protocol

If the CI job must be renamed (e.g., to `ci` or `test`):

1. Update `jobs.<new-name>:` in `.github/workflows/ci.yml`
2. Update the branch protection rule to reference the new name
3. Both changes must land in the same PR; do NOT merge the rename alone or the gate disappears between steps

---

## Trigger Events

The workflow responds to these PR events only:

| Event | Trigger condition |
|-------|-------------------|
| `opened` | PR created and marked ready for review |
| `synchronize` | New commits pushed to an open PR |
| `reopened` | Closed PR re-opened |
| `ready_for_review` | Draft converted to ready for review |

Draft PRs (`github.event.pull_request.draft == true`) are explicitly excluded via the job condition. CI does NOT run on direct pushes to any branch.
