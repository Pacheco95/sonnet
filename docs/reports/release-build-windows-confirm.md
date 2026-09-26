# Windows Release build fix — confirmation (issue #36)

Confirms `fix/release-build` at `2efd44a` against the failures recorded in `docs/reports/release-build-windows.md`: MSVC plain Release failing on `modules/rhi/src/OwnerThread.h:16` (C4100) and `modules/rhi/src/VulkanDevice.cpp:795` (C4189), and clang-cl failing every translation unit on `/Zc:preprocessor`. Same machine as that report: Windows 11 (build 10.0.26200), Visual Studio 18 Community (`cl.exe` 19.50.35729.0), clang-cl 23.1.1 from the same LLVM install, `VCPKG_ROOT` at the manifest's baseline.

Six configure+build passes, each in its own fresh directory, `-- -k 0`:

| Build | `CMAKE_BUILD_TYPE` | Compiler |
|---|---|---|
| `build/confirm-msvc-relwithdebinfo` | RelWithDebInfo | `cl.exe` |
| `build/confirm-msvc-release` | Release | `cl.exe` |
| `build/confirm-msvc-debug` | Debug | `cl.exe` |
| `build/confirm-clangcl-relwithdebinfo` | RelWithDebInfo | `clang-cl.exe` |
| `build/confirm-clangcl-release` | Release | `clang-cl.exe` |
| `build/confirm-clangcl-debug` | Debug | `clang-cl.exe` |

All six configures exited 0. All six builds exited 0, reached ninja's last step at `[472/472]`, and produced zero `FAILED:` lines and zero compiler diagnostics of any kind (`warning:`/`error:` for clang-cl, `warning C####`/`error C####` for MSVC — none appear in any of the six logs). `.obj` counts: 148 for each MSVC build, 146 for each clang-cl build (the two-object difference is consistent across all three build types for a given compiler, so it tracks a compiler-specific artifact of the shared precompiled header rather than anything build-type-specific). Since nothing failed, `-k 0` had no opportunity to demonstrate the "keep going after a failure" behavior the previous report couldn't observe past `sonnet_rhi` — there was no failure to schedule around.

clang-cl compiled every translation unit in the project for the first time (engine sources, tests, and all app `main.cpp` files, across all three build types) with no errors and no warnings.

## Test results

`ctest --test-dir <dir> --output-on-failure`: 100% of 12 suites passed in all six builds.

| Build | Suites | Result | Total time |
|---|---|---|---|
| MSVC RelWithDebInfo | 12/12 | Passed | 34.07 s |
| MSVC Release | 12/12 | Passed | 28.53 s |
| MSVC Debug | 12/12 | Passed | 36.01 s |
| clang-cl RelWithDebInfo | 12/12 | Passed | 33.85 s |
| clang-cl Release | 12/12 | Passed | 28.42 s |
| clang-cl Debug | 12/12 | Passed | 38.85 s |

## `core_tests "[assert]"`, run standalone on each Release-type build

| Build | Summary |
|---|---|
| MSVC RelWithDebInfo | `All tests passed (4 assertions in 2 test cases)` |
| MSVC Release | `All tests passed (4 assertions in 2 test cases)` |
| clang-cl RelWithDebInfo | `All tests passed (4 assertions in 2 test cases)` |
| clang-cl Release | `All tests passed (4 assertions in 2 test cases)` |

Identical result on every Release-type build across both compilers: the compiled-out `SONNET_ASSERT` evaluates nothing, and `SONNET_VERIFY` still evaluates its expression.

## Anything else that failed

Nothing. All six configures and all six builds exited 0, with no diagnostics, no failed objects, and no test failures anywhere.

## Conclusion

`fix/release-build` resolves both failures from the original report. MSVC Release no longer trips on the two assertion-only values (`OwnerThread.h`'s parameters, `VulkanDevice.cpp`'s `expected`), and clang-cl — building this project for the first time — compiles and links every configuration cleanly with no warnings. All six Debug/RelWithDebInfo/Release builds across both compilers pass their full test suites.
