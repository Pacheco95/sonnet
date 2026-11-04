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

## What CI should show

These are four local CPUs, which ran the suites about twice as fast as a runner does. The CI baseline is [run 41915804872](https://github.com/Pacheco95/sonnet/actions/runs/41915804872), in which `editor_tests` took 241 s of 416 s of suites on Linux GCC, 244 of 421 on Linux Clang, 239 of 421 on Linux ASan, 221 of 365 on Linux GCC Release and 143 of 221 on Linux Clang Release. With two suites at once a Linux job's Test step can be no shorter than its `editor_tests`, and the local runs suggest about 20% more than that suite's serial time: roughly 290 s for the Debug and ASan jobs where the Test step took about 420 s. Those are estimates; the pull request's run has the measured ones. The ASan job's `editor_tests` keeps a 600 s timeout, and `runtime_tests`, the next longest at 107 s, its 300 s. The Windows, macOS and TSan jobs spend 40 to 90 s in tests and gain less.

Splitting `editor_tests` ([#469](https://github.com/Pacheco95/sonnet/issues/469)) is what would shorten the run further.
