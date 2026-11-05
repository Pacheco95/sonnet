# Linux CI packages

Measurements for [issue #471](https://github.com/Pacheco95/sonnet/issues/471), on 2026-11-05: every Linux build job installed the same packages, LLVM's included in the GCC jobs, and the install step took from 57 s to 297 s. What each job needs is in [build.md](../build.md#linux-packages).

## Baseline

[Run 42050881275](https://github.com/Pacheco95/sonnet/actions/runs/42050881275), a push to `main` at 8ef66b4, all jobs started within a minute of each other on `ubuntu-24.04` hosted runners (image 20261101.360.1, ASan on 20261025.353.1). The install step runs before any cache is restored, so the vcpkg and sccache entries the jobs then restored (each group's newest from `main`) do not bear on it. The step's phases, read from the timestamps of its log:

| Job | Step, s | Add the kisak PPA and update, s | `llvm.sh 20`, s | `apt-get update`, s | System packages: download, s | Unpack and the rest, s |
|---|--:|--:|--:|--:|--:|--:|
| Linux GCC | 124 | 33 | 17 | 2 | 65 | 7 |
| Linux GCC Release | 57 | 10 | 23 | 3 | 9 | 12 |
| Linux Clang | 57 | 13 | 22 | 4 | 10 | 8 |
| Linux Clang Release | 117 | 9 | 16 | 4 | 80 | 8 |
| Linux ASan | 297 | 20 | 24 | 4 | 236 | 13 |
| Linux TSan | 215 | 11 | 22 | 4 | 169 | 9 |

Lint's "Check formatting" step, `llvm.sh 20` and then `clang-format-20`, took 30 s. The Android job's install step, without LLVM or the PPA, took 20 s.

- **`llvm.sh 20`** adds apt.llvm.org's source, which runs another index update, and installs `clang-20`, `lldb-20`, `lld-20` and `clangd-20` with their recommends: 22 packages, 158 MB, 765 MB unpacked, replacing the image's `lldb-18`. It downloads at 39 to 140 MB/s, so its 16 to 24 s are mostly unpacking. It ran in the GCC jobs and in lint too, where nothing uses it but `clang-format-20`, installed after it.
- **The system packages** fetch 81.8 MB, of which 71.6 MB come from the kisak PPA (Launchpad): `libllvm21` 31 MB, `mesa-vulkan-drivers` 28 MB and `mesa-libgallium` 12 MB. That download went at 0.35 to 8.9 MB/s between jobs of the same run, and it is most of the spread: 9 s in two jobs, 236 s in ASan. The Ubuntu mirror's 7.8 MB and apt.llvm.org's `clang-tidy-20`, 2.1 MB, are the rest.
- **The indexes** were read three times: by `add-apt-repository`, by `llvm.sh`'s own `add-apt-repository`, and by the step's `apt-get update`.

The test summaries of the same run, which the change must leave alone: each suite passed; `rhi_tests` and `rhi_tests_vulkan_1_3` ran 49 entries with 2 skipped in every Lavapipe job, and 49 with 40 skipped in TSan, which runs no driver; `editor_tests` ran 321 entries with none skipped, and 317 with 115 skipped in TSan.

## The change

Each job installs only what it runs ([build.md](../build.md#linux-packages)): no LLVM in the GCC jobs, `clang-format-20` alone in lint, `clang-20` and `llvm-20` without recommends in the Clang jobs, `libclang-rt-20-dev` added in the sanitizer jobs, `clang-tidy-20` only in the Linux Clang job of a pull request, and no Mesa in TSan. The sources are added before a single `apt-get update`. Every job but TSan still downloads the PPA's Mesa, which is what Lavapipe 1.4 runs on, so its install time keeps the PPA's spread.

Ubuntu 24.04's own `noble-updates` now carries Mesa 25.2.8, whose Lavapipe gives Vulkan 1.4.318, and would replace the PPA's 72 MB with about 30 MB from the Ubuntu mirror. That is a change of the Mesa version the GPU tests and golden images run on, 26.2.6 today, so it is left for its own measurement.
