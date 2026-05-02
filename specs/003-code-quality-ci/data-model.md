# Data Model: Code Quality Enforcement (Formatter + SAST)

**Branch**: `003-code-quality-ci` | **Date**: 2026-05-01

This feature introduces no runtime data entities. All "data" is configuration versioned in the repository. The three key entities from the spec are mapped to concrete files here.

---

## Format Rule Set → `.clang-format`

**What it represents**: The project-wide style definition that governs what "correctly formatted" means. Every source file in `src/` and `tests/` must conform.

**Location**: `<repo-root>/.clang-format`

**Key attributes**:
- `BasedOnStyle`: base style template (`LLVM`)
- `ColumnLimit`: maximum line width (`100`)
- `Standard`: C++ standard awareness (`c++23`)
- Scope: applied to `*.cpp`, `*.h` files under `src/` and `tests/`; excludes `vendor/` and `build*/`

**Lifecycle**: Created once; updated only by deliberate maintainer decision. Committed to main branch. All contributors and CI use identical settings.

---

## SAST Configuration → `.clang-tidy` + cppcheck flags

**What it represents**: The set of static analysis rules applied to the codebase and the severity threshold that determines blocking vs. non-blocking findings.

**Location (clang-tidy)**: `<repo-root>/.clang-tidy`

**Key attributes (clang-tidy)**:
- `Checks`: comma-separated list of enabled check groups (`bugprone-*`, `clang-analyzer-*`, `modernize-*`, `performance-*`, `readability-*`)
- `WarningsAsErrors`: checks promoted to errors (all enabled checks → `*`)
- `HeaderFilterRegex`: restricts header analysis to project headers only (excludes `_deps/`, `vendor/`)

**cppcheck flags (no config file — embedded in CI/local scripts)**:
- Blocking: `--enable=warning,performance,portability --error-exitcode=1`
- Non-blocking: `--enable=information` (separate invocation, exit code ignored)
- Exclusions: `--suppress=*:build*/*` `--suppress=*:vendor/*`

**Lifecycle**: Created once; updated when maintainers deliberately add/remove rules. Committed alongside source.

---

## Quality Gate → GitHub Actions job `code-quality`

**What it represents**: The CI enforcement mechanism that runs format check and SAST before any build or test job, and blocks PR merging on failure.

**Key attributes**:
- Job name: `code-quality`
- Trigger: same as `build-and-test` (PR to `main`, non-draft only)
- Downstream dependency: `build-and-test` declares `needs: [code-quality]`
- Blocking condition: non-zero exit from clang-format check, or any clang-tidy/cppcheck finding at warning-or-above
- Non-blocking output: informational cppcheck findings posted as `level: warning` reviewdog annotations (do not fail the job)
- Annotation mechanism: `reviewdog` with `github-pr-review` reporter → inline diff comments on PR Files view
- Infrastructure failure behaviour: job marked failed/cancelled; PR not blocked; contributor must re-trigger

**Relationships**:
- Consumes: Format Rule Set (`.clang-format`), SAST Configuration (`.clang-tidy`), `compile_commands.json` (generated during configure step)
- Produces: inline PR annotations, job pass/fail status checked by GitHub branch protection
