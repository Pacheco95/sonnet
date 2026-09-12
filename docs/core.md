# core

Fundamental types every other module uses. `core` depends on GLM, spdlog and Tracy only. Public headers live under `sonnet/core/`.

| Header | Contents |
|---|---|
| `Handle.h` | `Handle<Tag>`: 32-bit slot index plus 32-bit generation, typed by a tag struct, hashable, packable into a `uint64_t` |
| `HandlePool.h` | `HandlePool<T, Tag>`: slot map that issues handles, bumps the generation on release and refuses stale handles |
| `Log.h` | `Log` and the `SONNET_LOG_*` macros |
| `Assert.h` | `SONNET_ASSERT` and `SONNET_VERIFY` |
| `Error.h` | `Error`, `Result<T>` (`std::expected<T, Error>`) and `Exception` |
| `Uuid.h` | 128-bit identifier, random generation, canonical string form |
| `Math.h` | The single GLM include point; checks that the GLM configuration is present |
| `Profile.h` | `SONNET_ZONE()`, `SONNET_ZONE_NAMED()`, `SONNET_FRAME_MARK()` over Tracy |
| `Version.h` | `engineVersion()`, the version from the root `CMakeLists.txt` |

## Handles

A handle is a value type components store and owners resolve ([architecture.md](architecture.md#resource-handles)). `HandlePool` is the reference owner: `emplace` returns a handle, `remove` returns the removed object so the caller decides when it is destroyed (for GPU objects, after the device is idle), and a handle whose slot was reused never resolves again because the generation moved on. `get` asserts on a stale handle; `find` returns null instead for callers that expect staleness.

## Logging

`sonnet_add_module` and `sonnet_add_module_test` define `SONNET_MODULE` to the target's name, and the `SONNET_LOG_*` macros use it to pick the logger, so `SONNET_LOG_INFO("created {}", name)` in `rhi` sources goes to the `rhi` logger. Loggers are created on first use and share one sink list; `Log::addSink` reaches existing and future loggers, which is how the editor's log panel attaches. The line format and levels are in [conventions.md](conventions.md#logging). `SPDLOG_ACTIVE_LEVEL` is set by the build per configuration, so trace and debug calls compile out of Release.

## Assertions and errors

`SONNET_ASSERT(expr, fmt, args...)` is compiled in when `SONNET_ASSERTS_ENABLED` is defined, which the build does for Debug and RelWithDebInfo; `SONNET_VERIFY` always evaluates its expression. A failure logs the expression and the `std::source_location` of the site at `critical`, raises `SIGTRAP` (or `__debugbreak`) so an attached debugger stops there, then aborts.

`Error` and `Exception` capture `std::source_location` at construction by a defaulted parameter, so the reported line is where the problem was created. Return `Result<T>` from recoverable operations, throw `Exception` from initialisation.

## GLM

The four GLM configuration macros are `PUBLIC` compile definitions of `sonnet::core`; no translation unit defines them itself. Include GLM through `sonnet/core/Math.h`, which fails to compile when the definitions are missing.

## Tests

`core_tests` covers handles and the pool, logging through a captured sink, error locations, UUID generation and parsing, and the version. Run one tag with `core_tests "[handle]"`.
