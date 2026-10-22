# KosmicKrisp timestamp investigation

[Issue 374](https://github.com/Pacheco95/sonnet/issues/374), measured on
2026-10-22 on the [issue 368 trial](kosmickrisp-macos.md)'s Apple M4 Max,
macOS 26.7.1, Xcode 26.2 and LunarG SDK 1.4.363.0. The driver identifies as
KosmicKrisp 26.2.99, `vulkan-sdk-1.4.363 (git-269a955c89)`.

**Cause isolated below Sonnet:** on this machine, the fifth and subsequent
relaxed Metal 4 compute timestamps in one encoder resolve to zero.
KosmicKrisp exposes those zeros as available Vulkan queries. Precise compute
sampling repairs both a raw Vulkan reproduction and the basic sample, without
changing Sonnet's recording, query reset, availability handling or conversion.
An independent Metal program reproduces the four/five-sample boundary without
loading Vulkan or Sonnet.

This change retains the regression, reproduction sources and repair experiment.
It does **not** repair the installed SDK or change the shipping backend. A
production driver fix, its performance qualification and a renewed migration
trial remain external follow-ups. Keep the shipping MoltenVK selection and use
Release wall clock for comparisons with the unmodified KosmicKrisp SDK.

## Raw evidence and mechanism

Temporary logging immediately after `vkGetQueryPoolResults` in
`VulkanDevice::readTimestamps` reproduced the basic sample's failure. All 2,144
query results in that capture had availability 1. The first submission read
back normal clocks at indices 0–3, zeros at 4–36, and a normal clock again at
37. Later render/compute boundaries repeat the pattern. Consequently:

- Two zero endpoints appear as an available zero-duration pass.
- A normal-to-zero pair is rejected as decreasing, so the pass has no available
  timing samples even though the driver marked both queries available.
- A zero-to-normal pair becomes an enormous positive duration. Shadow cascade
  0 and depth late averaged **460,126,880 ms** while frames were **17.26 ms**
  apart in this reproduction.

The graph's missing occlusion-cull samples therefore do not establish missing
Vulkan availability. The raw values explain both the missing and enormous
measurements before any double conversion or float accumulation.

The retained RHI diagnostic allocates its own Vulkan command pool, command
buffer and timestamp pool. It records 34 consecutive `ALL_COMMANDS` timestamps,
then three clear-only dynamic render passes with timestamps inside and outside
each rendering instance. It submits directly and waits for queue idle before
reading interleaved 64-bit values and availability. It bypasses the render graph,
frame-slot bookkeeping and RHI nanosecond conversion. The driver creates compute
work for the out-of-render timestamp writes itself.

With stock KosmicKrisp, queries 0–3 have the same normal tick value, 4–33 have
zero, and 34 onward return normal values across the render boundaries. Every
query is available and `vkGetQueryPoolResults` returns `VK_SUCCESS`. The test
fails the clock-order check at query 4 and the independently measured host-time
bound at query 34. A four-query version of the same experiment passed; simply
increasing the consecutive compute samples reproduced the scene failure.

The SDK's `VERSIONS.txt` identifies the exact vendor fork revision as
[`269a955c898df283fc36f4827643769068cf3d29`](https://gitlab.freedesktop.org/aitor/mesa/-/tree/269a955c898df283fc36f4827643769068cf3d29).
Its relevant sources are:

- [`kk_query_pool.c`](https://gitlab.freedesktop.org/aitor/mesa/-/blob/269a955c898df283fc36f4827643769068cf3d29/src/kosmickrisp/vulkan/kk_query_pool.c):
  compute timestamp writes issue a small compute write and sample the Metal
  counter heap. The resolved value replaces an unavailable sentinel; zero
  differs from that sentinel, so it is exposed as available. Graphics-stage
  samples already reuse a sample within a render encoder.
- [`mtl_encoder.m`](https://gitlab.freedesktop.org/aitor/mesa/-/blob/269a955c898df283fc36f4827643769068cf3d29/src/kosmickrisp/bridge/mtl_encoder.m):
  both bridges request relaxed sampling.
- [`kk_cmd_buffer.c`](https://gitlab.freedesktop.org/aitor/mesa/-/blob/269a955c898df283fc36f4827643769068cf3d29/src/kosmickrisp/vulkan/kk_cmd_buffer.c):
  ending an encoder resolves its pending counter samples into the query data.

The [independent Metal reproduction](kosmickrisp-timestamps/metal-probe.m)
issues one no-op compute dispatch before each timestamp, ends the encoder,
waits on a shared event, and resolves the heap on the CPU. It tests fresh
encoders/heaps with four, five and sixteen writes under both granularities.
Relaxed sampling gives four identical normal clocks followed by zeros; precise
sampling gives ordered, nonzero clocks for every sample. Metal commit feedback
reports successful GPU execution in all six cases. This proves the zero result
also occurs below the Vulkan implementation; the public behavior alone does
not identify which private Apple implementation component is responsible.

Apple's [compute timestamp API](https://developer.apple.com/documentation/metal/mtl4computecommandencoder/writetimestamp(granularity:into:at:))
allows relaxed sampling to group timestamps and precise sampling to cost more.
Grouping explains identical clocks, but does not make a later zero on an
otherwise advancing clock a usable interval. A driver-side repair can request
precise compute samples; its overhead needs measurement before adoption.
Dropping every zero would also drop a legitimate available zero clock, and
clamping intervals would conceal the failure. Neither is done here.

## Repair experiment and independent measurement

[precise-compute.m](kosmickrisp-timestamps/precise-compute.m) is a diagnostic
library loaded only into the chosen test process. It replaces the Metal compute
encoder's timestamp granularity with precise and adds Metal commit feedback.
It leaves render sampling, query indices, resets, resolves and Sonnet's code
unchanged. No SDK file is modified, and the library is never linked into Sonnet.

| Check | Stock KosmicKrisp | Precise compute experiment |
|---|---|---|
| Debug raw Vulkan diagnostic | Fails ordering and host bound | 134 assertions pass |
| Release raw Vulkan diagnostic | Same two failures | 134 assertions pass |
| Independent Metal probe, 4/5/16 writes | Relaxed loses samples after four | Precise returns every sample |
| Basic Debug capture | Enormous durations and missing pass samples | Plausible durations, including compute passes |

The same Debug raw diagnostic passes **134 assertions** on the old SDK's
MoltenVK **1.4.1** control. This control uses SDK 1.4.341.1 explicitly; it is
not a measurement of the statically shipped MoltenVK 1.4.2.

For the repaired Debug Vulkan diagnostic, all 44 queries were available and
ordered. Their first-to-last span was **6.382875 ms**. The independent Metal
commit feedback for that single submission measured **6.512042 ms** on the GPU;
host submission-to-idle elapsed time was **12.170334 ms**. Thus the query span
fits inside the GPU execution interval as well as the host bound. The retained
[verifier](kosmickrisp-timestamps/verify.py) checks this relationship using one
timestamp tick and printed-feedback rounding as the tolerance.

The repaired full basic capture reported a **5.457 ms** GPU pass sum, with
shadow cascade 0 **0.328 ms**, depth late **0.293 ms**, forward **0.741 ms**,
occlusion cull **0.012 ms**, and depth pyramid **0.072 ms**. Each steady pass had
27 available samples; the counters include startup and the frame-slot delay.
Independent Metal feedback placed the subsequent scene submissions in
**4.893–7.906 ms**, excluding the first 59.890 ms initialization submission;
small extra submissions were below 0.1 ms. These windows are a plausibility
comparison, not an exact frame pairing. The single-submission diagnostic above
provides the exact pairing. Precise sampling's overhead means this capture is
not a performance comparison with stock relaxed sampling.

The existing Debug RHI suite on stock KosmicKrisp still passes **4,561
assertions, 42 cases**, with six headless-swapchain cases skipped. No validation
warning/error was counted in the raw diagnostic, repaired capture or suite.

## Reproduction

The RHI test is opt-in under its sole tag `[.timestamp-diagnostic]`, so ordinary
GPU-suite filters do not silently start running a known failing driver probe.
It is a white-box test linked to the existing Vulkan implementation interface;
no public API or application target changes.

Build `rhi_tests` and `sonnet_player_app` with the local macOS preset, then set
the trial environment from [the original report](kosmickrisp-macos.md#machine-and-driver-selection).
These commands assume that SDK is still in `/tmp/sonnet-368/sdk`:

```sh
export SDL_VULKAN_LIBRARY=/tmp/sonnet-368/sdk/macOS/lib/libvulkan.1.dylib
export VK_DRIVER_FILES=/tmp/sonnet-368/sdk/macOS/share/vulkan/icd.d/libkosmickrisp_icd.json
export VK_LAYER_PATH=/tmp/sonnet-368/sdk/macOS/share/vulkan/explicit_layer.d
build/macos-debug-local/modules/rhi/rhi_tests '[.timestamp-diagnostic]' > /tmp/timestamps-stock.log 2>&1
```

That diagnostic is expected to fail on the measured SDK. It prints every raw
clock and availability before asserting, so a failure retains useful evidence.
Repeat with `build/macos-release-local/modules/rhi/rhi_tests` for Release.

Compile the independent Metal reproduction with Xcode 26 on macOS 26 or newer:

```sh
xcrun clang -fobjc-arc -mmacosx-version-min=26.0 -framework Foundation -framework Metal docs/reports/kosmickrisp-timestamps/metal-probe.m -o /tmp/metal-timestamps
/tmp/metal-timestamps
```

Compile and run the diagnostic repair experiment in a fresh process:

```sh
xcrun clang -fobjc-arc -mmacosx-version-min=26.0 -dynamiclib -framework Foundation -framework Metal docs/reports/kosmickrisp-timestamps/precise-compute.m -o /tmp/precise-compute.dylib
DYLD_INSERT_LIBRARIES=/tmp/precise-compute.dylib build/macos-debug-local/modules/rhi/rhi_tests '[.timestamp-diagnostic]' > /tmp/timestamps-precise.log 2>&1
python3 docs/reports/kosmickrisp-timestamps/verify.py /tmp/timestamps-precise.log
DYLD_INSERT_LIBRARIES=/tmp/precise-compute.dylib build/macos-debug-local/apps/player/sonnet_player /tmp/sonnet-368/basic --settle-frames 24 --screenshot /tmp/timestamps-precise.png
```

`DYLD_INSERT_LIBRARIES` is set for each experiment command only. For the
MoltenVK control, omit it and select SDK 1.4.341.1's loader and
`MoltenVK_icd.json`. The original trial's prepared basic project was used here;
the [preparation script](kosmickrisp-macos/prepare.py) can recreate a temporary
copy without changing tracked sample assets.

[Archived logs](kosmickrisp-timestamps/logs.tar.gz) retain the instrumented stock
capture, both Debug controls, both Release results, the independent Metal probe,
the repaired full capture with GPU feedback, and the regular RHI suite. No
upstream report was submitted by this change. The remaining qualification gate
is a production KosmicKrisp/Metal fix passing the unmodified raw diagnostic and
scene capture without this experimental shim, followed by performance checks.
