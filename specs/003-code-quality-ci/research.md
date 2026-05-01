# Research: Code Quality Enforcement (Formatter + SAST)

**Branch**: `003-code-quality-ci` | **Date**: 2026-05-01

## 1. Formatter Selection

**Decision**: `clang-format`

**Rationale**: clang-format is the de-facto standard formatter for C/C++ projects. It is part of the LLVM toolchain (Apache-2.0), natively available on all CI platforms, ships alongside clang-tidy (same package), and produces deterministic output from a versioned `.clang-format` config file. Check mode (`--dry-run --Werror`) exits non-zero on any violation without modifying files; fix mode (`-i`) rewrites files in-place — satisfying FR-004, FR-005, and FR-006.

**Alternatives considered**:
- `uncrustify`: FOSS, but configuration is verbose, less ecosystem tooling
- `astyle`: FOSS, limited C++23 awareness, smaller ecosystem

**Base style chosen**: `LLVM` with `ColumnLimit: 100`. LLVM style is the default for the clang project itself and maps naturally to C++23 projects; a 100-column limit is a common modern compromise between readability and horizontal space.

---

## 2. SAST Tool Selection

**Decision**: `clang-tidy` (primary) + `cppcheck` (secondary)

**Rationale**: No single FOSS SAST tool covers all relevant bug classes for C++23. clang-tidy performs deep semantic, AST-level analysis (use-after-move, null dereferences, modernization, performance anti-patterns) but requires a compilation database (`compile_commands.json`). cppcheck performs fast pattern-based analysis (bounds overflows, uninitialized variables, resource leaks, integer overflows) without requiring a build, and catches distinct classes of bugs from clang-tidy. Together they provide complementary coverage with acceptable CI overhead.

Both tools are FOSS (LLVM Apache-2.0 / cppcheck GPL-3.0), satisfying FR-007.

**clang-tidy check groups enabled**:
- `bugprone-*` — latent bugs and unsafe patterns
- `clang-analyzer-*` — Clang Static Analyzer checks (deeper data-flow)
- `modernize-*` — C++11/14/17/23 upgrades
- `performance-*` — unnecessary copies, move semantics
- `readability-*` — code clarity (with identifier naming rules disabled to avoid churn)

**cppcheck check groups enabled (blocking)**: `warning,performance,portability`
**cppcheck check groups surfaced as non-blocking warnings**: `information`

This directly implements the spec clarification: warnings and above block the PR; informational findings are shown without blocking.

**Alternatives considered**:
- `clang-tidy` alone: misses pattern-level checks cppcheck covers, and is slower — pairing with cppcheck adds coverage with minimal overhead
- `PVS-Studio`: proprietary, not FOSS ✗
- `Infer` (Meta): JVM-based, high setup cost, primarily Java/C focus

---

## 3. CI Annotation Integration

**Decision**: `reviewdog` with `github-pr-review` reporter

**Rationale**: reviewdog (Apache-2.0) is the standard FOSS tool for converting linter/formatter output to inline GitHub PR annotations. The `github-pr-review` reporter posts findings as review comments directly on affected diff lines (matching spec clarification Q1). It supports clang-format, clang-tidy, and cppcheck natively via dedicated GitHub Actions (`reviewdog/action-clang-format`, `reviewdog/action-clang-tidy`, `reviewdog/action-cppcheck`). Each action accepts a `level` parameter — setting `level: error` for blocking checks and `level: warning` for informational findings maps exactly to the two-tier severity model in FR-003 and FR-003a.

**Alternatives considered**:
- `cpp-linter/cpp-linter-action`: combines clang-format + clang-tidy but does not support cppcheck; less composable
- Native `::error` GitHub annotations: requires custom parsing scripts per tool; high maintenance

---

## 4. compile_commands.json Generation

**Decision**: `cmake -S . -B build-quality -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_BUILD_TYPE=Debug`

**Rationale**: CMake generates `compile_commands.json` automatically during the configure step when `CMAKE_EXPORT_COMPILE_COMMANDS=ON` is set. No build (compilation) is required — only `cmake -S . -B <dir>` is needed. This is necessary for clang-tidy to resolve includes and understand the compile flags (`-std=c++23`, `-Wall`, etc.). The code-quality CI job performs this configure step using the same dependency cache key as the build-and-test job, ensuring there is no duplicated FetchContent download overhead in steady state.

cppcheck does not require `compile_commands.json` and runs directly against the source tree.

---

## 5. Vendor/Generated Code Exclusion

**Decision**: Exclude `build-quality/_deps/`, `vendor/`, and `build-quality/` from both tools.

**Rationale**: clang-tidy is invoked via `run-clang-tidy` with a path filter restricting analysis to `src/` and `tests/` only, which naturally excludes FetchContent deps in `build-quality/_deps/`. cppcheck is invoked with explicit `--suppress` paths for `vendor/` and `build-quality/`. This directly satisfies FR-008.

---

## 6. Integration Summary

| Tool | Role | Config File | Local Command | CI Action |
|------|------|-------------|---------------|-----------|
| clang-format | Formatting | `.clang-format` | `clang-format --dry-run --Werror` | `reviewdog/action-clang-format@v1` |
| clang-tidy | Semantic SAST | `.clang-tidy` | `run-clang-tidy -p build` | `reviewdog/action-clang-tidy@v1` |
| cppcheck | Pattern SAST | inline flags | `cppcheck --enable=warning,...` | `reviewdog/action-cppcheck@v1` |
| reviewdog | Annotation router | n/a | n/a | posts `github-pr-review` comments |
