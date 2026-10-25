# iPad GPU timestamp reproduction

Reproduction of [issue 152](https://github.com/Pacheco95/sonnet/issues/152),
2026-10-12. **The A16 iPad also loses host timestamp availability after a
completed frame timeline.** Loss was intermittent in these runs, so the fixed
capture summaries continued to report nonzero averages rather than `n/a`.
The earlier [iPhone investigation](iphone-m14-timestamps.md) explains the
reporting fix and its independent Metal comparison.

## Setup

- Source: `9a4b82ebbec3a738c7823926ac5e4defd6d370ed`, engine 0.18.0.
- Device: iPad (A16), iPad15,7, iPadOS 27.0; Apple A16 GPU,
  MoltenVK 1.4.2 / Vulkan 1.4.357.
- Build: `ios-release`, RelWithDebInfo, optimization level 2 and `NDEBUG`.
- Fresh cook of the unchanged `apps/samples/basic` for iOS. Its stress scene
  and script match the earlier investigation's `7189689` baseline.
- Actual render size: **750×1334**, FIFO. The app declares `UIDeviceFamily = [1]`
  and runs in iPhone compatibility mode on the iPad. This is a different
  workload from the iPhone's 1290×2796; frame costs are not a device comparison.
- Every run starts a fresh process with `--terminate-existing --console`,
  `--scene scenes/stress.scene.json --play 3 --screenshot <unique-name>.png`.
  All eight runs exit `ok`, write their screenshots and report nominal thermal
  state throughout their sampled windows. No sustained or cooling test was run.

## Uninstrumented captures

The three retrieved engine-written PNGs show the expected box-and-sphere grid.
All 23 passes have nonzero means in all three summaries.

| Run | CPU ms | Interval ms | GPU mean sum ms |
|---|---:|---:|---:|
| 1 | 6.25 | 20.33 | 18.049 |
| 2 | 6.43 | 20.33 | 18.799 |
| 3 | 6.21 | 20.33 | 18.328 |

These summaries exclude unavailable samples and do not expose sample counts.
Consequently, they cannot establish that every frame supplied timestamps.

## Raw-query diagnostics

A temporary logger immediately after the existing `vkGetQueryPoolResults`
recorded the submission, return code, availability of every query, raw ticks
and timestamp period. Query flags, synchronization, conversion and aggregation
were unchanged. Logging can perturb scheduling, so these runs are diagnostic
evidence, not baseline performance measurements.

| Run | Stress reads | Stress reads with 0/46 available | Unavailable in final 100 reads | GPU summary ms |
|---|---:|---:|---:|---:|
| 1 | 179 | 111 | 55 | 18.428 |
| 2 | 179 | 77 | 28 | 18.157 |
| 3 | 179 | 122 | 65 | 17.547 |

All unavailable stress reads return `VK_NOT_READY`; all other stress reads
return success with 46/46 available. Each process has 190 reads including
initialization. Across all three processes, every one of the 5,626 available
begin/end pairs has a positive difference; none is equal or reversed. Differences
range from 9,167 to 14,339,667 ns at the reported 1 ns/tick period.
The final 100 query reads include the capture frame and are not claimed to be
exactly the same window as the summary, which excludes that frame.

## Completed-timeline controls

Two further runs use the same control as the iPhone report: after an
ordinary not-ready 46-query read, inspect the timeline counter, then retry the
same query pool with `VK_QUERY_RESULT_WAIT_BIT` into a separate array. The
ordinary results remain untouched. The explicit wait changes scheduling and
is solely a diagnostic control, not a proposed per-frame change.

| Run | Not-ready reads | Counter equals submitted value | Retry succeeds, 46/46 available | Wait min / median / max ms |
|---|---:|---:|---:|---|
| 1 | 130 | 130 | 130 | 0.027 / 0.458 / 0.921 |
| 2 | 126 | 126 | 126 | 0.037 / 0.408 / 0.874 |

All 256 initial reads have 0/46 available, and all timeline counter queries
succeed. For example, control 1 submission 13 has counter 13; a query wait
recovers all 46 results after 156.916 microseconds, with first/last ticks
`32375254572958` and `32375272679458` (18.1065 ms apart).

This reproduces the iPhone's delayed host query availability after a completed
frame timeline on another Apple GPU. It does not establish the precise Metal
callback ordering. No independent Metal trace was collected on this iPad,
and the all-missing summary window seen on the iPhone was not reproduced.

## Artifacts and cleanup

Local artifacts are under `build/issue-152-ipad/`: three baseline PNGs,
filtered run/diagnostic/control logs, private full logs, diagnostic patches,
build logs and a saved baseline app. Private logs and device identifiers are
not committed. The baseline executable SHA-256 is
`b883fa76713fbaa611d8f4c52eb090dea9649bf6387ebd1994920e0324237927`;
the cooked bundle SHA-256 is
`d7bf39e006c0bc8387ec563a2adee8dc05def32fac563782382b6584e3bea317`.

Temporary source instrumentation was removed, the ordinary Release build was
rebuilt successfully, and the saved baseline app was reinstalled on the iPad.
No engine behavior change is retained.
