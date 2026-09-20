# core

Fundamental types every other module uses. `core` depends on GLM, spdlog and Tracy only. Public headers live under `sonnet/core/`.

| Header | Contents |
|---|---|
| `Handle.h` | `Handle<Tag>`: 32-bit slot index plus 32-bit generation, typed by a tag struct, hashable, packable into a `uint64_t` |
| `HandlePool.h` | `HandlePool<T, Tag>`: slot map that issues handles, bumps the generation on release and refuses stale handles |
| `Log.h` | `Log`, the `SONNET_LOG_*` macros and `SONNET_LOG_LOCATED` |
| `Assert.h` | `SONNET_ASSERT` and `SONNET_VERIFY` |
| `Error.h` | `Error`, `Result<T>` (`std::expected<T, Error>`) and `Exception` |
| `Uuid.h` | 128-bit identifier, random generation, name-based derivation for sub-assets, canonical string form |
| `JobSystem.h` | `JobSystem` and `JobHandle`: the engine's one thread pool, with dependencies, a parallel for and main-thread affinity |
| `File.h` | `readFile`: whole-file read returning `Result<std::vector<std::byte>>`; `writeFile`: whole-file write that creates the directories |
| `Math.h` | The single GLM include point; checks that the GLM configuration is present |
| `Profile.h` | `SONNET_ZONE()`, `SONNET_ZONE_NAMED()`, `SONNET_ZONE_NAME()`, `SONNET_SET_THREAD_NAME()`, `SONNET_FRAME_MARK()` over Tracy |
| `Version.h` | `engineVersion()`, the version from the root `CMakeLists.txt` |

## Handles

A handle is a value type components store and owners resolve ([architecture.md](architecture.md#resource-handles)). `HandlePool` is the reference owner: `emplace` returns a handle, `remove` returns the removed object so the caller decides when it is destroyed (for GPU objects, after the device is idle), and a handle whose slot was reused never resolves again because the generation moved on. `get` asserts on a stale handle; `find` returns null instead for callers that expect staleness. `forEach` visits the live objects, for leak reports and statistics.

## The job system

`JobSystem` is the engine's one thread pool ([ADR-0013](decisions/0013-job-system.md)). Everything the engine schedules runs on it: the renderer's per-frame upload, the asset imports and Jolt's jobs. flecs is the exception and keeps stage workers of its own, because a flecs worker blocks for a whole pipeline run rather than running to completion, and a pool whose workers are blocked is a pool that starves everything else.

`schedule` takes a name, a callable and the handles it depends on, and returns a `JobHandle`; the job runs once, on a worker, after every dependency has finished. `wait` blocks until a handle is done and **runs other jobs while it waits**, so a job may wait on the jobs it scheduled without deadlocking a pool whose every worker is doing the same. `parallelFor` splits a range into contiguous chunks of at least a grain, runs them across the pool and waits, keeping the last chunk for the calling thread rather than idling it.

`JobSystemDesc::workerCount` is `hardware_concurrency() - 1` when it is unset, leaving the calling thread a core of its own. Zero means no workers at all, and every job then runs on the thread that waits for it, which is what the tests and the cook tool use. Workers are named `sonnet worker <n>` for Tracy and carry a zone per job, named after the job.

Work that may only happen on the main thread — creating an `rhi` resource, above all — goes to `scheduleOnMainThread` and runs when the application loop calls `runMainThreadJobs`. Waiting on the main thread drains that queue too, so waiting for a main-thread job is not waiting for yourself. A job that throws is caught and logged rather than terminating the process, because a job that never completes is a waiter that never wakes.

## Logging

`sonnet_add_module` and `sonnet_add_module_test` define `SONNET_MODULE` to the target's name, and the `SONNET_LOG_*` macros use it to pick the logger, so `SONNET_LOG_INFO("created {}", name)` in `rhi` sources goes to the `rhi` logger. Loggers are created on first use and share one sink list; `Log::addSink` reaches existing and future loggers, which is how the editor's log panel attaches. The line format and levels are in [conventions.md](conventions.md#logging). `SPDLOG_ACTIVE_LEVEL` is set by the build per configuration, so trace and debug calls compile out of Release. `SONNET_LOG_LOCATED(level, location, ...)` is for records whose origin is not a C++ line: it takes a runtime level and a `spdlog::source_loc`, which the scripting runtime fills with a script's file and line.

## Assertions and errors

`SONNET_ASSERT(expr, fmt, args...)` is compiled in when `SONNET_ASSERTS_ENABLED` is defined, which the build does for Debug and RelWithDebInfo; `SONNET_VERIFY` always evaluates its expression. A failure logs the expression and the `std::source_location` of the site at `critical`, raises `SIGTRAP` (or `__debugbreak`) so an attached debugger stops there, then aborts.

`Error` and `Exception` capture `std::source_location` at construction by a defaulted parameter, so the reported line is where the problem was created. Return `Result<T>` from recoverable operations, throw `Exception` from initialisation. The category says which layer failed: platform, graphics, shader, I/O or script.

## GLM

The four GLM configuration macros are `PUBLIC` compile definitions of `sonnet::core`; no translation unit defines them itself. Include GLM through `sonnet/core/Math.h`, which fails to compile when the definitions are missing.

## Tests

`core_tests` covers the job system — dependencies, a job waiting on its own children, `parallelFor`'s coverage of a range, main-thread affinity, a throwing job and the jobs queued at destruction — handles and the pool, logging through a captured sink, error locations, UUID generation, derivation and parsing, file reads and writes, and the version. Run one tag with `core_tests "[handle]"`.
