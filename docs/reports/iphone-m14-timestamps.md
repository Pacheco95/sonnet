# iOS GPU timestamp investigation

Investigation of [issue 152](https://github.com/Pacheco95/sonnet/issues/152), 2026-10-10. The earlier device observations are in the [iPhone M14 report](iphone-m14.md); their numbers are not repeated here. **Verdict: MoltenVK or platform limit. Host query availability trails the completed frame timeline on the measured configuration; the old reporting path treated missing samples as zero.**

## Setup and access

Device validation started from current `main`, `7189689e21c9053cf53b72ac20e49e6d10611ec4`
(PR 161), on `mac-validation/m14-timestamps-device`. CoreDevice details succeeded
outside the sandbox; the paired device's tunnel connected on demand. The signed
`ios-release` (RelWithDebInfo) player built and installed successfully from this
commit. The basic sample was cooked with `sonnet_cook --platform ios`; every run
uses its unchanged `scenes/stress.scene.json`.

The device reports iOS 27.0; its GPU and player log confirm Apple A17 Pro and
MoltenVK 1.4.2 / Vulkan 1.4.357. The inherited Homebrew `CC` and `CXX` initially
broke the KTX dependency build; explicitly selecting Xcode's Apple Clang for both
variables resolved it. Device discovery, signing details and identifiers are
excluded from this report.

## Reproduction and measurements

The engine-written stress PNG was retrieved and visually confirms the expected
box-and-sphere grid. Every short run uses `--console --terminate-existing`, `--play 3` and
`--screenshot stress.png`, starts a fresh process and ends with `exit ok`.
Idle gaps below run from the preceding process's `exit ok` to the next first
engine log, so include launch overhead. The owner reported the device as
slightly warm during this sequence; no instrumented thermal state was collected.
Initial idle duration before short 1 was not measured. Each capture log has
four initialization warnings that blending is enabled for an `R32_UINT`
attachment, and no engine error or MoltenVK error message. These warnings occur
in both timing-success and timing-loss runs; their causal role is not established
and no rendering change was made.

| Run | Idle before launch | CPU ms | Interval ms | GPU total ms | Passes without samples |
|---|---|---:|---:|---:|---|
| Short 1 | Not measured; first after install | 6.14 | 22.23 | 21.728 | None |
| Short 2 | 7.19 s | 6.28 | 22.26 | n/a | All 23 |
| Short 3 | 8.58 s | 6.34 | 22.39 | n/a | All 23 |
| Short 4 | 8.45 s | 6.51 | 22.41 | 21.978 | None |
| Short 5 | 6.48 s | 6.41 | 22.51 | n/a | All 23 |
| Cooling repeat of short 1 | 15 min 18 s; owner reported cool | 6.45 | 22.22 | n/a | All 23 |
| Sustained, 600 seconds of simulation | Not measured; after Metal checks | 9.60 | 45.85 | n/a | All 23 |

The frame-times summary covers the last 100 rendered frames. `--play` takes
simulation seconds: the sustained command is `--play 600`, about 36,000 fixed
steps; a 600-step capture would be `--play 10`. These are new measurements;
[earlier observations](iphone-m14.md) remain a separate baseline.

The pass order is: cull; shadow cascades 0–3; depth; depth pyramid; occlusion
cull; depth late; light clustering; forward; taa; bloom down 0–4; bloom up 3–0;
tonemap; present. All 23 print `n/a` in short runs 2, 3 and 5, the cooling repeat,
and the sustained run. The sustained fresh process ran from its first engine log
to `exit ok` for **24 min 15.9 s**, with no `entering the background` message.
The owner confirmed the screen would stay awake and the player remain in front.
This log duration excludes pre-log launch overhead; the actual rendered frame
count is not printed by the baseline.

| Pass | Short 1 ms | Short 4 ms |
|---|---:|---:|
| cull | 0.098 | 0.098 |
| shadow cascade 0 | 0.016 | 0.023 |
| shadow cascade 1 | 0.168 | 0.194 |
| shadow cascade 2 | 0.741 | 0.752 |
| shadow cascade 3 | 4.855 | 4.870 |
| depth | 1.285 | 1.387 |
| depth pyramid | 1.033 | 1.059 |
| occlusion cull | 0.034 | 0.036 |
| depth late | 0.009 | 0.122 |
| light clustering | 0.011 | 0.012 |
| forward | 9.018 | 9.090 |
| taa | 3.186 | 3.292 |
| bloom down 0 | 0.375 | 0.325 |
| bloom down 1 | 0.099 | 0.018 |
| bloom down 2 | 0.014 | 0.030 |
| bloom down 3 | 0.007 | 0.009 |
| bloom down 4 | 0.043 | 0.005 |
| bloom up 3 | 0.005 | 0.007 |
| bloom up 2 | 0.023 | 0.023 |
| bloom up 1 | 0.004 | 0.013 |
| bloom up 0 | 0.232 | 0.013 |
| tonemap | 0.196 | 0.275 |
| present | 0.278 | 0.324 |

## Diagnostic findings

Two fresh-process `--play 3` runs used temporary per-frame logging in
`VulkanDevice::readTimestamps`, with no changes to the query flags, synchronization,
conversion or aggregation. Both ended with `exit ok` and all 23 passes `n/a`.

| Diagnostic run | Query reads | Successful reads | Not-ready reads | Last 100 reads | CPU / interval ms |
|---|---:|---:|---:|---|---|
| 1 | 190 | 11 | 179 | All `VK_NOT_READY`, 0/46 available | 6.40 / 22.34 |
| 2 | 190 | 12 | 178 | All `VK_NOT_READY`, 0/46 available | 6.39 / 22.55 |

| Question | Measured answer |
|---|---|
| Unavailable, equal ticks, or conversion rounding? | In each run the last 100 reads return `VK_NOT_READY` (1), with all 46 availability values zero. Their zero-filled tick storage is not a measurement. Across both runs, all 370 available begin/end pairs have positive differences; none are equal or reversed. Positive differences range from 1,917 to 10,076,750 ns. |
| Early frames versus the last 100? | Each run begins with an unavailable 100-query read during initialization, followed by ten successful 30-query reads for the smaller graph. Stress reads then lose availability: diagnostic 1 has one successful 46-query read and 178 wholly unavailable ones; diagnostic 2 has two successful 46-query reads, 176 wholly unavailable ones and one partially available read (3/46). Neither last-100 window has an available pair. |
| Period and valid bits? | Both runs report `timestampPeriod = 1.0` ns/tick and `timestampValidBits = 64` on every read. Available raw ticks are around 12.7 trillion, below the integer-exact range limit of double precision; multiplying by 1.0 preserves them. Conversion cannot explain the observed unavailable queries. |
| Completed slot and query-pool ownership? | Baseline code waits for the slot's submitted timeline value, reads that slot's query pool, then resets and records it. Two additional fresh-process runs measure a completed timeline counter before querying again with `VK_QUERY_RESULT_WAIT_BIT`: all 355 initially unavailable stress reads recover 46/46 samples after the wait. See the control experiment below. |
| Counter sample buffer or fallback? | The independent device Metal trace contains 46 `Record GPU Counter Sample BlitEncoder` records in a representative stress frame. Matching MoltenVK source creates these only for deferred stage-counter queries with a Metal counter buffer, establishing that path in the traced run. The fallback is not responsible for that run. The two diagnostic runs' path was not separately observed. |
| Do graph passes share Metal encoders? | The trace's representative frame has 19 render, 19 dispatch and 46 sample blit encoders, plus three other blits. Labels are generic; graph pass names are not attached. Exact pass-to-encoder mapping and sharing were not established, and per-pass timestamp boundaries cannot be equated with individual Metal encoder durations. |
| Pacing, power and temperature? | Short runs stay near 22 ms apart whether GPU timings are present or absent. The cooled repeat still has no samples. The Metal trace's thermal track is Nominal throughout its 21.04 s recording. The sustained Power Profiler trace reports Serious throughout its retained final 120 s. Its requested rolling one-minute window retained two minutes; the rest of the run is not retained. Neither this state nor the sustained interval establishes a thermal cause for the missing timestamps. |

For example, diagnostic 1 reads submission 2 successfully with 30/30 available,
first tick `12701821504875`, last tick `12701825777375`, period 1.0 and 64 valid
bits. Submissions 188–190 return `VK_NOT_READY`, 0/46 available, and zero-filled
first/last storage. Diagnostic 2 ends with the same unavailable pattern.

MoltenVK 1.4.2's [query-pool implementation](https://github.com/KhronosGroup/MoltenVK/blob/v1.4.2/MoltenVK/MoltenVK/GPUObjects/MVKQueryPool.mm)
marks host availability in `finishQueries`; its
[command encoder](https://github.com/KhronosGroup/MoltenVK/blob/v1.4.2/MoltenVK/MoltenVK/Commands/MVKCommandBuffer.mm)
invokes that from a Metal command-buffer completion handler and emits the
observed sample blits for stage counters. These are source mechanisms to test,
not proof of a particular callback-ordering failure on this device.

### Completed-timeline control experiment

After the sustained run, two additional fresh processes used the raw logger plus
one temporary diagnostic: on a not-ready 46-query read, read the timeline counter,
then read the same pool into a separate array with `VK_QUERY_RESULT_WAIT_BIT`.
The ordinary sample array and its conversion remain unchanged; the added wait
can affect subsequent frames, so these are control experiments, not baseline
performance runs.

| Control | Initially unavailable reads | Timeline already complete | Query wait recovered | Wait minimum / median / maximum |
|---|---:|---|---|---|
| 1 | 177 | 177/177, counter equals submitted value | 177/177, success and 46/46 available | 0.237 / 0.873 / 1.972 ms |
| 2 | 178 | 178/178, counter equals submitted value | 178/178, success and 46/46 available | 0.182 / 0.851 / 2.063 ms |

Both controls exit `ok`; their unchanged ordinary-read summaries still have all
23 passes `n/a`, at CPU/interval **9.27/40.65 ms** and **8.57/35.01 ms**.
For example, control 1 submission 13 has timeline counter 13 and counter-query
success, yet the ordinary query read returns `VK_NOT_READY`. The query wait
returns success with 46/46 available after 0.896 ms; first/last ticks are
`14459085936041` and `14459108462291` (22.526 ms apart at the measured period).
Every last-100 ordinary read in both controls is unavailable; every corresponding
wait succeeds.

This establishes **host timestamp availability later than the frame's completed
ALL_COMMANDS timeline**, with valid samples recovered by an explicit query wait.
The exact ordering of Metal completion callbacks was not directly traced;
MoltenVK's completion-handler implementation explains a mechanism consistent
with the measured dependency, not a separately observed callback sequence.
No blocking query wait is proposed for normal engine frames.

## Independent Metal comparison

A device Metal System Trace captured the unchanged baseline stress scene,
including its fresh-process `--play 3` run and screenshot. The recording's
initial document lacked template metadata; importing its recorded data with the
explicit Metal System Trace template recovered usable tables. Neither the trace
nor its XML exports are in the repository.

GPU work is the union of the player's Active intervals across Compute, Vertex
and Fragment channels, grouped by Instruments frame number. Overlapping channels
are counted once. There are 192 player frame groups including initialization;
the last 100 have mean active GPU time **22.623 ms**, median **22.459 ms**, range
**22.055–25.795 ms**. Their earliest-to-latest GPU span averages **22.986 ms**,
which includes gaps and is a different quantity.

The same run's engine summary reports **22.272 ms GPU**, **5.55 ms CPU** and
**22.91 ms interval**, with no pass printing `n/a`. The engine-to-active-Metal
mean difference is **0.351 ms (1.55%)**. This supports agreement of whole-frame scale; individual passes were not
independently validated. The baseline summary does not print
available-sample counts, so intermittent losses within its average are unknown.

The XML carries integer nanoseconds and formats most individual intervals in
microseconds. Comparisons of whole-frame means use **0.8 ms** as a coarse window
alignment tolerance: two frames of query readback latency plus one final
screenshot frame can displace a 100-frame window by three frames; three times
the observed maximum active frame time divided by 100 is 0.774 ms. This tolerance
covers that frame granularity, not unknown sample-selection bias. The observed
0.351 ms difference falls within it. Exact frame alignment and per-pass tolerance
are not established.

In representative Instruments frame 150, active time per render encoder ranges
from **0.006 to 9.074 ms** (19 encoders), dispatch encoder from **0.0017 to
0.604 ms** (19), and counter-sample blit from **0.0015 to 0.0568 ms** (46).
Selected render-encoder active intervals, in GPU-start order, are **4.984 ms**,
**1.337 ms**, **9.074 ms** and **3.223 ms**. Generic labels prevent an independent
named-pass assignment. Tile execution overlaps encoder spans, so summing their
start-to-end spans would overcount; the figures above union the channels within
each encoder instead.

## Change and verification

PR 161 already preserves timestamp availability and prints missing pass samples
as `n/a`. This device work has not changed engine behavior, the stress scene,
render passes or pacing, and leaves the version unchanged. The signed baseline
and temporary diagnostic builds both compiled successfully; the diagnostics are
restored out of the working tree after building and are not committed as code.

The baseline app executable SHA-256 is
`770bb0b25a7ffbd3707e472c5d0ff1a1b5ca835a37f574821f54513262f7a8e1`;
the iOS cooked bundle SHA-256, identical in baseline and diagnostics, is
`0a5f61573e8ca030363b234c8d981975ce3be127dd4e30b650b7cea5c05b8f13`.
The baseline was saved before diagnostic builds and reinstalled for the Metal
comparison and sustained run, and restored after the control experiments.

`python3 tools/check_docs.py`, `git diff --check`, the requested privacy scan,
and clang-format checks of the baseline and temporary diagnostic C++ all pass.
Only this report and the iOS section of `docs/player.md` are committed; traces,
XML exports, device logs and temporary C++ patches remain outside the repository.

## Verdict

**MoltenVK or platform limit**, specifically delayed host query availability on
Apple A17 Pro / iOS 27.0 / MoltenVK 1.4.2 relative to the engine's completed-frame
timeline wait. Two ordinary diagnostic runs lose every sample in their last-100
windows; two control runs prove that 355 not-ready reads occur after the timeline
counter reaches the submitted value and recover all samples with a query wait.
The engine's nonblocking readback correctly reports `n/a` after PR 161. No new
engine defect or behavior change was established, so no fix commit or version
change is needed here.

The device uses working counter buffers and stage sampling; available samples
have positive differences at the reported 1.0 ns period and 64 valid bits. The
independent Metal trace measures real GPU work near the nonzero engine totals.
Equal timestamps, conversion rounding and lack of counter-buffer support do not
explain the measured availability loss. This documents a limit of the measured nonblocking readback behavior on this
configuration.

Before PR 161, missing queries became numerical zero and the capture average
included those zeroes, so fewer available frames could dilute the reported GPU
means. The measured availability loss is consistent with the
[earlier dropping totals](iphone-m14.md), but those captures have no raw-query
dumps; their exact availability history is not established.

An upstream MoltenVK issue worth filing is **timestamp host availability remains
zero after an ALL_COMMANDS timeline wait on Apple A17 Pro / iOS 27.0, then
recovers with query WAIT**. Include the return codes, counter values, availability
counts, wait delays and counter-stage Metal trace figures above. No upstream
issue was filed.

Not established: the specific Metal callback sequence behind the delay, exact
named-pass-to-encoder sharing, individually validated pass timings, baseline
sample counts within nonzero summaries, the initial idle duration, or thermal
causality. The retained thermal trace covers only the end of the sustained run.
No conclusion about the sustained slowdown in issue 159 is made here.

## Appendix: temporary raw-query logging

The following addition immediately after `vkGetQueryPoolResults` is the only
change in the two diagnostic captures. It leaves the query result and existing
conversion untouched. It was built locally, removed from engine code afterward,
and formatted with the repository's clang-format style.

```cpp
static const auto validBits =
    m_physicalDevice.getQueueFamilyProperties()[m_graphicsFamily].timestampValidBits;
std::uint32_t availableCount = 0;
std::string samples;
for (std::uint32_t i = 0; i < frame.timestampCount; ++i) {
  availableCount += raw[2 * i + 1] != 0 ? 1u : 0u;
  samples += std::format("{}{}:{}", i == 0 ? "" : ",", raw[2 * i], raw[2 * i + 1]);
}
SONNET_LOG_INFO(
    "timestamp diagnostic submission={} result={} available={}/{} first={} last={} period={} bits={} samples={}",
    frame.submittedValue, static_cast<int>(result), availableCount, frame.timestampCount,
    raw[0], raw[2 * (frame.timestampCount - 1)], m_timestampPeriod, validBits, samples);
```

The control experiment additionally includes `<chrono>` and inserts this block
after the logger, before the existing return-code check. Its separate result
array is diagnostic only. The baseline app was restored after both controls.

```cpp
if (result == VK_NOT_READY && frame.timestampCount == 46) {
  std::uint64_t counter = 0;
  const VkResult counterResult =
      m_device.getDispatcher()->vkGetSemaphoreCounterValue(*m_device, *m_timeline, &counter);
  std::array<std::uint64_t, std::size_t{MaxTimestamps} * 2> retry{};
  const auto started = std::chrono::steady_clock::now();
  const VkResult retryResult = m_device.getDispatcher()->vkGetQueryPoolResults(
      *m_device, *frame.queryPool, 0, frame.timestampCount, sizeof(retry), retry.data(), 2 * sizeof(std::uint64_t),
      VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT | VK_QUERY_RESULT_WAIT_BIT);
  std::uint32_t retryAvailable = 0;
  for (std::uint32_t i = 0; i < frame.timestampCount; ++i) {
    retryAvailable += retry[2 * i + 1] != 0 ? 1u : 0u;
  }
  SONNET_LOG_INFO("timestamp retry submission={} counterResult={} counter={} result={} available={}/{} wait_us={} "
                  "first={} last={}",
                  frame.submittedValue, static_cast<int>(counterResult), counter, static_cast<int>(retryResult),
                  retryAvailable, frame.timestampCount,
                  std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - started).count(),
                  retry[0], retry[2 * (frame.timestampCount - 1)]);
}
```
