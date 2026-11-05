# Parallel test suites

Measurements for [issue #468](https://github.com/Pacheco95/sonnet/issues/468), on 2026-11-04: CI ran the CTest suites one after another, and the question was how many to run at once on a hosted runner, and what had to change first so that suites running together do not share writable files.

## Shared state

Each suite already had a temporary directory of its own (`tests/support/TestSupport.cpp`, [#398](https://github.com/Pacheco95/sonnet/issues/398)), and the Catch2 reports are named after their CTest test ([#467](https://github.com/Pacheco95/sonnet/issues/467)). What was still shared:

- **The samples' asset caches.** `runtime_tests` opened `apps/samples/basic`, `hook` and `showcase` of the checkout in place, and two `assets_tests` cook cases opened `basic`. Opening a project imports its textures into `<project>/.sonnet/cache`, and `core::writeFile` truncates and rewrites an entry in place, so one suite could read another's half-written entry. The `editor_tests` cases that edit a sample copy it first, but the copy took `.sonnet` along: with the suites one after another it held `runtime_tests`' finished cache, and with them together whatever part of it existed at that moment. The import treats an entry no older than its source as fresh, so a truncated copy would fail the case, and a missing one changed how long it took.
- **Nothing else.** Editor preferences and `layout.ini` are not written by a headless editor unless a test points it at a file in its temporary directory, and the tests that check the per-user directory redirect it first; captures go to the temporary directory; golden images are written only with `SONNET_UPDATE_GOLDEN=1`; `rhi_tests` and `rhi_tests_vulkan_1_3`, one binary, write differently named reports; the Adreno rewrite check uses a `tempfile` directory; Mesa's shader cache handles concurrent processes itself. CTest writes `ctest.xml` and `LastTest.log` itself: under `--parallel 2` `LastTest.log` still has 15 complete output blocks in registration order, and `ctest.xml` one entry per suite.

The fix gives each suite its own copy instead of a lock: `sonnet::test::copySample` copies a sample without `.sonnet` and `export`. `runtime_tests` and the cook cases copy each sample once per process and share it between their cases, as they shared the checkout's cache before; the `editor_tests` cases still copy per case. A `RESOURCE_LOCK` on the samples would have kept `editor_tests` and `runtime_tests`, the two longest suites, from overlapping, which is where the time is. After every run below, `apps/samples/basic`, `hook` and `showcase` had no `.sonnet` folder: no suite wrote into the checkout.

The editor's copies now start without a cache, as they do from a fresh clone. That did not slow `editor_tests`: one after another, its time went from 109.0 s on `main` to 99.7 s and 101.9 s, within the noise of the unchanged suites, which moved by the same few percent.

## Method

- A Debug build with GCC 14 (`linux-debug`) and an ASan build with Clang 20 (`linux-asan`), on Lavapipe (Mesa 25.2.8, LLVM 20.1.2), on an 8-core, 16-thread desktop limited to four logical CPUs with `taskset -c 0-3`, the vCPU count of a Linux runner. CTest 4.4.3.
- The command CI runs, `ctest --preset <preset> --parallel N --timeout 300 --output-junit test-results/ctest.xml`, in a systemd user scope. Memory is two figures: the scope's cgroup `memory.peak`, and the peak of the summed `VmRSS` of the scope's processes sampled every 0.5 s. The cgroup misses file pages already cached by an earlier run, and the summed RSS counts shared libraries once per process, so the true peak lies between them.
- No `CTestCostData.txt` before a run, as in CI, and the samples' caches cleared before the `main` runs, as CI's checkout has none.

## Results

| Build | Suites at once | Runs | Wall time, s | `editor_tests`, s | Peak memory, GiB (cgroup / summed RSS) |
|---|--:|--:|---|---|---|
| Debug, `main` | 1 | 1 | 191.6 | 109.0 | 0.91 / 0.97 |
| Debug | 1 | 2 | 176.0, 180.4 | 99.7, 101.9 | 0.93 / 0.99 |
| Debug | 2 | 3 | 123.1, 124.3, 125.0 | the whole run | 1.22 / 1.45 |
| Debug, no `COST` | 2 | 1 | 150.0 | 122.7 | 0.92 / 1.09 |
| Debug | 3 | 1 | 124.4 | the whole run | 1.11 / 1.41 |
| ASan | 1 | 2 | 356.2, 356.4 | 214.3, 217.4 | 3.22 / 3.39 |
| ASan | 2 | 3 | 255.8, 243.4, 245.6 | the whole run | 3.61 / 5.97 |
| ASan | 3 | 2 | 248.7, 250.7 | the whole run | 3.90 / 4.32 |

Memory is the largest of the runs in the row. Every run passed, with the 15 tests and the same Catch2 entries as CI's baseline run (321 in `editor_tests`, 58 in `runtime_tests`, 4 skipped in all, the two compressed-image cases of each `rhi` registration, as in CI); there were no timeouts and no intermittent failures in the 16 runs. `main` itself also passed with two suites at once (123.9 s), so the shared cache was a race that had not yet been lost, not a failure seen.

- **Two suites at once** take 30% off the Debug run (176 to 180 s down to 123 to 125 s) and 30% off the ASan run (356 s down to 243 to 256 s). Both are now as long as `editor_tests`, which slows by about 20% with another suite beside it (100 to 124 s, 216 to 248 s), because Lavapipe and the job system keep more than one CPU busy in every suite.
- **Three suites at once** gain nothing more: the run is still `editor_tests`, which is as slow as with two, and the other suites slow more (`renderer_tests` 18 to 24 s in Debug, 47 to 64 s in ASan).
- **`COST`** on `editor_tests` makes it start first. Without it CTest starts the suites in registration order, so `editor_tests` began 27 s in, after the fourteen tests registered before it had run through two lanes, and the run took 150 s instead of 124 s.
- **Memory** grows from 3.4 GiB to at most 6 GiB under ASan, counted generously, on a runner with 16 GiB.

## Measured in CI

The baseline is [run 41915804872](https://github.com/Pacheco95/sonnet/actions/runs/41915804872), suites one after another; the pull request's [run 41930016334](https://github.com/Pacheco95/sonnet/actions/runs/41930016334) ran two at once. One run each, on hosted runners whose hardware varies from run to run. Test step durations:

| Job | One at a time, s | Two at once, s | Change |
|---|--:|--:|--:|
| Linux GCC | 417 | 196 | −53% |
| Linux Clang | 422 | 364 | −14% |
| Linux ASan | 422 | 361 | −14% |
| Linux GCC Release | 365 | 308 | −16% |
| Linux Clang Release | 221 | 211 | −5% |
| Linux TSan | 57 | 49 | −14% |
| macOS Clang | 92 | 37 | −60% |
| macOS Clang Release | 12 | 8 | −33% |
| Windows MSVC | 42 | 59 | +40% |
| Windows MSVC Release | 18 | 15 | −17% |
| Windows clang-cl Release | 20 | 16 | −20% |

From the jobs' timing artifacts (`tools/test_timings.py --baseline`):

- **Linux Clang and ASan** gained less than the local runs suggested. Beside `runtime_tests`, `editor_tests` slowed by about half, not a fifth: 244 to 363 s on Clang, 239 to 360 s under ASan, with `runtime_tests` 106 to 160 s and 91 to 130 s. The Test step is now `editor_tests`, so each job saved about 60 s, not the 130 s estimated. ASan's `editor_tests` stays well inside its 600 s timeout.
- **Linux GCC** overstates the gain: its `editor_tests` got faster despite the second suite, 241 to 195 s, and `runtime_tests` 107 to 81 s, so that run most likely had a faster runner as well.
- **Windows MSVC** is the one job that got slower: side by side its suites took two to five times as long (`assets_tests` 19 to 49 s, `physics_tests` 3 to 14 s, `editor_tests` 6 to 25 s), 17 s more in all. The Windows Release jobs still gained. This is left as it is: the loss is small next to the Linux jobs' gains, and one run cannot separate contention from runner variance.
- Every suite passed with the same entries as the baseline (321 in `editor_tests`, 58 in `runtime_tests`). Peak memory on the runners was not measured.

Splitting `editor_tests` ([#469](https://github.com/Pacheco95/sonnet/issues/469)) is what would shorten the run further.
