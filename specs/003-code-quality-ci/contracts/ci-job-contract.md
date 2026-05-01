# Contract: CI Quality Gate Job

**Feature**: Code Quality Enforcement (003)  
**Audience**: CI pipeline authors and contributors reading pipeline results

This document defines the observable behaviour of the `code-quality` GitHub Actions job.

---

## Trigger

Same conditions as `build-and-test`: pull_request to `main`, non-draft only (`ready_for_review` event).

---

## Job Ordering

```
code-quality ──► build-and-test
```

`build-and-test` declares `needs: [code-quality]`. If `code-quality` fails, `build-and-test` is skipped. If `code-quality` is cancelled due to an infrastructure error, `build-and-test` is also skipped.

---

## Steps and Outcomes

| Step | Tool | Blocks PR? | Annotation type |
|------|------|-----------|-----------------|
| Format check | clang-format | Yes (on violation) | Inline diff comment |
| SAST semantic | clang-tidy | Yes (warning+) | Inline diff comment |
| SAST pattern blocking | cppcheck warning+ | Yes (on finding) | Inline diff comment |
| SAST pattern advisory | cppcheck information | No | Inline diff comment (non-blocking) |

---

## Branch Protection Requirement

The `code-quality` job MUST be added to the repository's branch protection required status checks for `main`. A PR can only merge when this check reports `success`.

---

## Infrastructure Failure Behaviour

If the job fails due to infrastructure (runner crash, FetchContent network timeout, etc.) rather than a code quality issue, the job is marked `failure` or `cancelled`. This does NOT automatically block the PR in the same way as a quality finding — the contributor or maintainer must re-trigger the job via the GitHub Actions UI ("Re-run failed jobs") to obtain a valid result.

---

## Annotation Format

Reviewdog posts findings as GitHub Pull Request Review comments (reporter: `github-pr-review`). Each comment appears inline on the diff line of the offending file in the PR Files view. The comment body includes:

- Tool name
- Finding severity
- Human-readable description
- Check name (for clang-tidy) or rule category (for cppcheck)

---

## Scope

Both the format check and SAST tools scan the **entire codebase** (`src/`, `tests/`) on every run — not just changed files. Vendor and generated code (`vendor/`, `build*/`) are excluded.
