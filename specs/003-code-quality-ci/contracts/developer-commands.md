# Contract: Developer-Facing Quality Commands

**Feature**: Code Quality Enforcement (003)  
**Audience**: Project maintainers running tools locally

This document defines the stable command interface that maintainers use to run quality checks locally. These commands MUST remain consistent between local execution and the CI environment — same tools, same configuration files, same exit code semantics.

---

## Format Check (read-only)

**Command**:
```sh
find src/ tests/ -name "*.cpp" -o -name "*.h" | xargs clang-format --dry-run --Werror
```

**Exit codes**:
- `0` — all files conform to the style rules
- Non-zero — one or more files have formatting violations

**Output**: Each violation is printed to stderr as `<file>:<line>:<col>: error: ...`. No files are modified.

---

## Format Fix (in-place rewrite)

**Command**:
```sh
find src/ tests/ -name "*.cpp" -o -name "*.h" | xargs clang-format -i
```

**Exit codes**:
- `0` — all files have been rewritten to conform

**Output**: Files modified in-place. No terminal output unless an error occurs.

---

## SAST — clang-tidy (blocking threshold: warning and above)

**Prerequisite**: cmake configure must have been run with `CMAKE_EXPORT_COMPILE_COMMANDS=ON`:
```sh
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_BUILD_TYPE=Debug
```

**Command**:
```sh
run-clang-tidy -p build src/
```

**Exit codes**:
- `0` — no findings at warning severity or above
- Non-zero — one or more findings detected

**Output**: Each finding printed as `<file>:<line>:<col>: warning/error: <description> [<check-name>]`.

---

## SAST — cppcheck (blocking threshold: warning and above)

**Command**:
```sh
cppcheck src/ tests/ \
  --enable=warning,performance,portability \
  --error-exitcode=1 \
  --suppress="*:vendor/*" \
  --suppress="*:build*/*"
```

**Exit codes**:
- `0` — no findings at warning severity or above
- `1` — one or more findings detected

**Output**: Each finding printed as `<file>:<line>: <severity>: <description>`.

---

## SAST — cppcheck (informational, non-blocking)

**Command**:
```sh
cppcheck src/ tests/ \
  --enable=information \
  --suppress="*:vendor/*" \
  --suppress="*:build*/*"
```

**Exit codes**:
- Always `0` (informational findings do not fail the check)

**Output**: Informational findings printed to stderr for developer awareness.

---

## Tool Installation

| Tool | Install command (Ubuntu/Debian) | Minimum version |
|------|---------------------------------|-----------------|
| clang-format | `sudo apt install clang-format` | 14+ |
| clang-tidy | `sudo apt install clang-tidy` | 14+ |
| cppcheck | `sudo apt install cppcheck` | 2.10+ |

If a tool is not installed, each command exits immediately with a non-zero code and prints an error to stderr identifying the missing binary.
