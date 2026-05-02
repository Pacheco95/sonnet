# Quickstart: Code Quality Tools

This guide covers installing and running the formatting and static analysis tools locally.

## Prerequisites

These tools must be installed before running any quality check commands. They are the same tools used in CI.

### Linux (Ubuntu/Debian)

```sh
sudo apt install clang-format clang-tidy cppcheck
```

### macOS

```sh
brew install llvm cppcheck
# clang-format and clang-tidy are included in the llvm formula
export PATH="$(brew --prefix llvm)/bin:$PATH"
```

### Windows

Install LLVM from https://releases.llvm.org/ (provides clang-format and clang-tidy).  
Install cppcheck from https://cppcheck.sourceforge.io/.  
Ensure both are on your `PATH`.

---

## Format Check

Check whether all source files conform to the project style (non-modifying):

```sh
find src/ tests/ -name "*.cpp" -o -name "*.h" | xargs clang-format --dry-run --Werror
```

Exit code 0 = all files conform. Non-zero = violations found (files + lines printed to stderr).

## Format Fix

Auto-rewrite all non-conforming files in-place:

```sh
find src/ tests/ -name "*.cpp" -o -name "*.h" | xargs clang-format -i
```

---

## SAST — clang-tidy

Requires a configured build directory with `compile_commands.json`. If you have not configured yet:

```sh
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_BUILD_TYPE=Debug
```

Run analysis:

```sh
run-clang-tidy -p build src/
```

Exit code 0 = no findings. Non-zero = one or more warning-level findings.

## SAST — cppcheck (blocking checks)

```sh
cppcheck src/ tests/ \
  --enable=warning,performance,portability \
  --error-exitcode=1 \
  --suppress="*:vendor/*" \
  --suppress="*:build*/*"
```

## SAST — cppcheck (informational, non-blocking)

```sh
cppcheck src/ tests/ \
  --enable=information \
  --suppress="*:vendor/*" \
  --suppress="*:build*/*"
```

---

## CI Equivalent

The `code-quality` CI job runs all four commands above (plus reviewdog for inline PR annotations) before the build-and-test job. A passing local run means the CI quality gate will also pass.
