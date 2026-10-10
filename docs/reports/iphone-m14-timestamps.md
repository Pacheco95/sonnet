# iOS GPU timestamp investigation

Investigation of [issue 152](https://github.com/Pacheco95/sonnet/issues/152), 2026-10-10. The earlier device observations are in the [iPhone M14 report](iphone-m14.md); their numbers are not repeated here. **The reporting defect is fixed; the cause of the iOS drift is not established and the issue's Done when is not met.**

## Setup and access

The requested measurement target is Apple A17 Pro, iOS 27.0, MoltenVK 1.4.2, `ios-release` (RelWithDebInfo), running the basic sample's unchanged `scenes/stress.scene.json`. This investigation started from current `main`, `4087a74dcbe9fca5697c183e13ffa607fb5dc352`, on branch `mac-validation/m14-timestamps`. This is a newer baseline than the earlier report.

CoreDevice discovery succeeded and found one iOS 27.0 device, but its tunnel state was `disconnected`. Connecting and unlocking `<device>` was requested. No install, fresh-process launch, screenshot, or device timing was obtained in this investigation. Identifiers and discovery output are excluded from the repository.

## Reproduction and before/after measurements

| Required run | Before | After |
|---|---|---|
| Short 1 | Not run: device disconnected | Not run |
| Short 2 | Not run: device disconnected | Not run |
| Short 3 | Not run: device disconnected | Not run |
| Short 4 | Not run: device disconnected | Not run |
| Short 5 | Not run: device disconnected | Not run |
| Sustained, 600 frames | Not run | Not run |

There is no new evidence about run order, elapsed time since install or previous launch, thermal state, power state, or FIFO/60 fps pacing. The earlier report's sustained command uses `--play 600`, which means 600 seconds of simulation, not 600 frames. A 600-frame fixed-step capture uses `--play 10`; record the actual rendered frame count separately if pacing or fixed-step catch-up changes it.

## Diagnostic findings

| Question | Evidence and conclusion |
|---|---|
| Unavailable versus equal ticks versus conversion | No device query dump was obtained. Code inspection proves that `readTimestamps` previously replaced availability 0 with a numeric zero. `RenderGraph::readTimings` also rejected an available begin timestamp of zero. These are reporting defects, not proof of the cause of the earlier measurements. |
| Completed slot and query-pool ownership | `prepareSlot` obtains `m_frames[m_frameIndex]`, calls `waitForFrame` on that slot's `submittedValue`, then reads that slot's query pool before resetting its command pools and timestamp count. The submission signals the timeline at `eAllCommands`; `beginFrame` records the query-pool reset afterward. This inspection provides no evidence of reading a different or unfinished slot during a successful timeline wait. Runtime completion was not verified on iOS. |
| Period and valid bits | Support checks the graphics family's `timestampValidBits` and positive `limits.timestampPeriod`; conversion multiplies raw ticks by the cached period in double precision and stores integer nanoseconds. Neither the actual device period nor raw ticks were measured here, so conversion correctness on this run is unverified. |
| Metal counter path and encoder granularity | Upstream source has counter-buffer sampling, deferred stage sampling, and a fallback. The path selected on this device was not observed. |
| Pacing, thermal and power effects | Not measured. No correlation or causal claim is supported. |

MoltenVK 1.4.2's [query-pool implementation](https://github.com/KhronosGroup/MoltenVK/blob/v1.4.2/MoltenVK/MoltenVK/GPUObjects/MVKQueryPool.mm) creates a Metal counter sample buffer when available. If it has no counter buffer, `finishQueries` assigns the same elapsed-time value to all queries in its completion batch. Its [command encoder implementation](https://github.com/KhronosGroup/MoltenVK/blob/v1.4.2/MoltenVK/MoltenVK/Commands/MVKCommandBuffer.mm) defers stage-counter queries until the end of Metal encoding batches and samples them using lightweight blit encoders; other supported sampling points use direct counter sampling. These mechanisms justify inspecting counter-buffer creation and sampling flags during device diagnosis; they do not prove that this scene used a fallback or that its graph passes shared sampling boundaries.

The [device implementation](https://github.com/KhronosGroup/MoltenVK/blob/v1.4.2/MoltenVK/MoltenVK/GPUObjects/MVKDevice.mm) initializes `timestampPeriod` to 1.0 and excludes Apple GPUs from its subsequent period calibration. This is source evidence about the driver's intended conversion, not an independent validation of the device's tick values.

## Independent Metal comparison

No Metal System Trace or GPU frame capture was obtained because the device was disconnected. GPU frame time, per-encoder time, agreement with nonzero engine measurements, and a justified numerical tolerance remain unestablished. No trace is committed. Available equal timestamps cannot be treated as unavailable simply to make this comparison appear to pass.

## Change and verification

`rhi::Timestamp` carries nanoseconds and query availability separately. Vulkan preserves the availability bit; the Null device tracks which queries were actually written, so a hole between written indices is unavailable while a written zero is available. The renderer requires two available, ordered samples and preserves equal samples as a measured zero. Capture summaries average available samples only, print `n/a` for a pass without samples, and report the total as `n/a` if any listed pass lacks samples. The editor's timing display also prints `n/a` for unavailable timings. No render passes, scene content, submission, or pacing behavior changed.

| Deterministic case | Before | After |
|---|---|---|
| Null queries written at indices 0 and 2 | All three values zero; hole indistinguishable | 0 and 2 available at zero; 1 unavailable |
| Graph before a slot has timing results | Numeric zero | Unavailable |
| Graph with two available equal timestamps | Numeric zero, also used for missing data | Available measured zero |
| Capture with a measured-zero pass and an unavailable pass | Both counted as zero | Measured pass `0.000 ms`; missing pass and total `n/a` |
| Capture with missing samples followed by a valid 2 ms sample | Missing samples could lower the mean | Missing samples excluded; mean 2 ms |

The macOS Debug build compiled `rhi_tests`, `renderer_tests`, `runtime_tests` and the editor. Both `ctest --preset macos-debug-local -R rhi_tests --output-on-failure` entries passed. The focused Null-device run passed 56 assertions (8 cases passed, 3 skipped), graph tests passed 46 assertions in 6 cases, and capture tests passed 116 assertions (6 cases passed, 3 skipped). The attempted iOS release build failed while rebuilding `ktx:arm64-ios`: its dependency build selected Homebrew LLVM 22 libc++ headers, which rejected the target platform. No iOS compilation or installation is claimed. GPU/window-dependent cases skipped because this test environment could not create a usable Vulkan window; this is Null-device and compilation proof, not device GPU validation.

## Remaining acceptance work

Connect `<device>`, rebuild and install the unchanged baseline, cook with `sonnet_cook --platform ios`, and use `devicectl device process launch --console --terminate-existing --device <device-id>` for each fresh run. Record five short runs and their idle intervals and device state. Then collect per-frame query return code, availability count, raw ticks, period and valid bits, and identify the active MoltenVK sampling path. Keep diagnostic logging temporary unless it is needed for the fix.

Obtain independent Metal timings for the same scene, state and build; repeat five short runs and a 600-frame sustained capture after the change. Record available-sample counts as well as means so intermittent losses are visible. Choose and state a comparison tolerance from the measurement granularity. Until these steps are complete, the verdict is **rhi reporting defect fixed, iOS zero/drift cause unresolved**, not “platform limit documented.” If device evidence implicates MoltenVK, a minimal upstream issue about timestamp sample availability or equal samples on Apple A17 Pro/iOS 27.0 would be worth filing with raw queries and Metal timings; no upstream issue was filed.
