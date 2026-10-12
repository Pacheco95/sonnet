# macOS Release physics job pool exhaustion

Diagnosis of [issue #168](https://github.com/Pacheco95/sonnet/issues/168), on 2026-10-12.

## Evidence

The macOS Release CI failure on [PR #167, attempt 1](https://github.com/Pacheco95/sonnet/actions/runs/38092409104/attempts/1) ended with SIGBUS in `physics_tests`. The contact-event overflow warning was followed by two more fixture initializations; it did not identify the crashing case.

Existing September Release binaries passed 200 runs of each suite, but were older than the current source. A fresh build of `main` at `f6cccea`, using Apple Clang 17 on arm64 with `CMAKE_BUILD_TYPE=Release`, reproduced SIGBUS in `physics_tests` on iteration 18 of a loop running both suites. The random seed was 3565590147. The macOS diagnostic report identified this stack:

```text
JoltJobSystem::CreateJob + 84
JPH::PhysicsSystem::Update
PhysicsStep
world::World::progress
record(Fixture&, float)
events come in the same order whatever the worker count
```

At the fault, register `x0` held `4294967295` (`0xffffffff`), the invalid index returned by `FixedSizeFreeList::ConstructObject` when it has no space. The instruction at `CreateJob + 84` loaded a page pointer using that index. The pool has 2,048 slots. The assertion checking the index is compiled out in Release, so exhaustion became an invalid memory access rather than an assertion failure.

## Cause and fix

Jolt's barrier waiter can execute a job before the engine worker scheduled for it runs. The barrier then finishes, but the queued engine callback still holds a reference to the completed job. Repeated steps can accumulate these callbacks faster than the workers release them, exhausting the bounded pool. Waiting for callbacks only in the adapter's destructor protected teardown but did not protect the next step.

`JoltJobSystem::WaitForJobs` now calls Jolt's barrier wait and then waits for the adapter's queued callbacks to execute and release their references. The destructor uses the same wait. The job pool size and Jolt's dependency scheduling are unchanged; no test retry hides a failure.

The regression test blocks the only worker while the barrier executes one job. Before the fix the wait returns despite the queued callback still owning its reference; the test fails `REQUIRE_FALSE(returnedBeforeWorker)`. With the fix it waits until the worker is released, then reuses a one-slot pool for 64 further steps.

## Verification

The local presets choose the Xcode compiler; the Release override matches CI rather than the preset's usual RelWithDebInfo:

```sh
cmake --preset macos-release-local -DCMAKE_BUILD_TYPE=Release
cmake --build --preset macos-release-local --target physics_tests scripting_tests
./build/macos-release-local/modules/physics/physics_tests "[jobs]"
ctest --preset macos-release-local -R '^(physics|scripting)_tests$' --output-on-failure
```

The issue's request for a few dozen clean macOS CI runs remains a monitoring condition after the fix lands; a local stress loop cannot establish that history.

After the fix, the fresh Release binaries passed 200 iterations of each complete suite (400 runs total), and the focused CTest suites passed in both Release and Debug. The regression test was run against the old adapter first and failed, then passed with the fix. Documentation, dependency and formatting checks also passed.

An isolated Release ASan/UBSan build compiled, but the Xcode 26.2 sanitizer runtime hung before the test runner started. Sampling showed `AsanInitInternal → InitializeShadowMemory → get_dyld_hdr → dyld_shared_cache_iterate_text_swift → _Block_copy → malloc → AsanInitFromRtl → StaticSpinMutex::LockSlow`: initialization re-entered itself through an allocation. Running outside the sandbox made no difference. No local sanitizer pass is claimed; the Linux sanitizer jobs provide that check.
