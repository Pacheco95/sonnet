# macOS occlusion wall-clock measurements

Date: 2026-10-08. Only this report is published; no engine changes or local merge are included.

## Conditions and method

- Fetched origin, checked out `fix/macos-compiler-path` (`2e20d2b`) into a fresh local worktree, and merged `feat/occlusion-culling` at `7e5da69`. Local merge: `6a399b3`. The report branch starts separately from `origin/main` (`beb204f`).
- Built `macos-release`, **RelWithDebInfo**, with Apple Clang 17.0.0, `-O2`, and optimized Slang shaders. Explicit Xcode `CC`/`CXX` paths avoided the shell's Homebrew compiler, whose first build failed to find SDK headers. No measurements used that failed build or Debug.
- macOS 26.7.1, arm64; MoltenVK 1.4.1, Vulkan device API 1.4.334, loader 1.4.341. Device names and identifiers are omitted. `SDL_VULKAN_LIBRARY=/usr/local/lib/libvulkan.1.dylib` for every invocation. Default job pool: 13 workers; `SONNET_BENCH_WORKERS` unset. Tracy remained at the preset default, enabled. Vulkan validation is disabled by this build configuration; the zero-validation-message assertions remained intact but do not establish validation coverage.
- AC power and Low Power Mode off (`lowpowermode 0`) checked before and after collection. An external display was initially active; the operator disconnected it before the throwaway run. System display enumeration verified one active built-in display before and after collection. Other drawing apps were requested to be paused; the reply confirmed display disconnection only. No competing engine benchmark/build ran during timing. Background GPU activity was not independently profiled, so complete absence of other drawing is not verified.
- One throwaway invocation, then seven counted normal-order invocations and seven reverse-order invocations. Normal: off/no wall, on/no wall, off/wall, on/wall; reverse: precisely the opposite four `measure(...)` calls. D1, D2, E1, E2 and base-only each ran five counted invocations in normal order, sequentially, with source reset to baseline before applying each variant. No additional throwaways or discarded counted runs.
- Every case warms up for 30 frames, then measures 120 frames submitted back to back using `std::chrono::steady_clock`. The final `device.waitIdle()` and final-frame readback are inside the timed interval. “Wall clock” is that duration divided by 120: end-to-end throughput, including CPU work and waiting, not a pure GPU duration.
- “Total GPU” sums the printed per-pass timestamps of one completed frame, not an average of the 120 frames or an independent whole-frame timer. “CPU submitting” is the benchmark's median of 145 `endFrame` durations (all 150 frames except the first five). The tables then summarize those printed values across invocations.
- All cells are **median [minimum, maximum] in ms**, retaining the benchmark's three-decimal precision. A dash means the pass was absent. No timestamp zero was discarded: normal-order run 4, off/no wall, printed zero for every GPU pass and total GPU despite a normal wall-clock duration. This is a timestamp reporting anomaly, not a zero-duration GPU workload; its underlying cause was not investigated.
- All 40 invocations (one throwaway plus 39 counted), four cases each, completed with 16 passing assertions per invocation and no skipped cases. Only this benchmark ran against intentionally altered variants. Their expected indirect calls were adjusted from 14 to 12 for “on” after removing `depth late`; no other assertions changed. Image correctness was not tested, and E2 deliberately removes required synchronization. Experimental source edits were restored after collection.

Commands from the local experiment worktree (compiler paths supplied through the environment):

```sh
cmake --preset macos-release
cmake --build --preset macos-release --target renderer_tests
SDL_VULKAN_LIBRARY=/usr/local/lib/libvulkan.1.dylib \
  ./build/macos-release/modules/renderer/renderer_tests \
  'ten thousand draws and a hundred lights at 1080p'
```

## Step 1: baseline in both orders

### Normal order, seven invocations

| Metric (ms) | Off, no wall | On, no wall | Off, wall | On, wall |
|---|---:|---:|---:|---:|
| Wall clock | 2.402 [2.387, 2.423] | 2.527 [2.519, 2.538] | 1.513 [1.511, 1.525] | 1.326 [1.324, 1.331] |
| Total GPU | 2.321 [0.000, 2.343] | 2.446 [2.433, 2.462] | 1.452 [1.441, 1.460] | 1.261 [1.255, 1.271] |
| Depth pyramid | — | 0.103 [0.102, 0.104] | — | 0.084 [0.083, 0.085] |
| Forward | 1.351 [0.000, 1.368] | 1.360 [1.350, 1.367] | 0.376 [0.366, 0.380] | 0.254 [0.249, 0.256] |
| Shadow cascade 3 | 0.368 [0.000, 0.374] | 0.368 [0.365, 0.371] | 0.368 [0.363, 0.373] | 0.369 [0.368, 0.374] |
| CPU submitting | 0.312 [0.306, 0.318] | 0.421 [0.377, 0.446] | 0.248 [0.238, 0.251] | 0.289 [0.283, 0.290] |

### Reverse order, seven invocations

| Metric (ms) | Off, no wall | On, no wall | Off, wall | On, wall |
|---|---:|---:|---:|---:|
| Wall clock | 2.393 [2.384, 2.401] | 2.527 [2.521, 2.546] | 1.518 [1.509, 1.528] | 1.329 [1.325, 1.336] |
| Total GPU | 2.325 [2.310, 2.341] | 2.448 [2.440, 2.460] | 1.460 [1.453, 1.472] | 1.260 [1.247, 1.280] |
| Depth pyramid | — | 0.103 [0.100, 0.105] | — | 0.084 [0.083, 0.085] |
| Forward | 1.354 [1.348, 1.361] | 1.354 [1.352, 1.369] | 0.373 [0.371, 0.376] | 0.252 [0.250, 0.255] |
| Shadow cascade 3 | 0.368 [0.366, 0.370] | 0.370 [0.368, 0.375] | 0.373 [0.368, 0.374] | 0.374 [0.368, 0.378] |
| CPU submitting | 0.314 [0.298, 0.328] | 0.410 [0.368, 0.412] | 0.226 [0.225, 0.234] | 0.291 [0.286, 0.302] |

## Step 2: wall-clock verdict

| Order and scene | On minus off, difference of wall-clock medians | Off spread (max − min) | On spread (max − min) | Verdict |
|---|---:|---:|---:|---|
| normal, no wall | +0.125 ms (+5.2%) | 0.036 ms | 0.019 ms | Real slowdown |
| normal, wall | -0.187 ms (-12.4%) | 0.014 ms | 0.007 ms | Faster with occlusion |
| reverse, no wall | +0.134 ms (+5.6%) | 0.017 ms | 0.025 ms | Real slowdown |
| reverse, wall | -0.189 ms (-12.5%) | 0.019 ms | 0.011 ms | Faster with occlusion |

Without the wall, “on” is slower by more than either run-to-run spread, and its minimum exceeds the off maximum in both orders. The roughly 0.13 ms penalty is real; it cannot be dismissed as a timestamp artefact.

With the wall, “on” is faster in every counted comparison: approximately 0.19 ms, or 12%. Under the requested wall-clock verdict, the previously reported per-pass slowdown for this scene is a timestamp artefact rather than an end-to-end slowdown. The large old Debug timestamp inflation itself is not reproduced by this Release benchmark; these runs do not isolate why those earlier timestamps differed. The current Release GPU medians mostly agree with wall clock, apart from the explicit zero sample above. Neither order supports a several-millisecond throughput regression.

## Step 3: pyramid variants, five normal-order invocations each

| Variant | Local change from baseline |
|---|---|
| D1 | Keep `depth pyramid`; omit `occlusion cull` and `depth late`; set `depthJob->pyramid = 0`. Forward still uses the depth job. |
| D2 | D1 with the entire `recordPyramid` body omitted; keep the graph pass, depth sampling declaration and transitions. |
| E1 | D1 with `levels = min(levels, 5)` in `ensurePyramid` and the matching loop bound in `recordPyramid`: base plus four reduction levels, rather than base plus ten. Allocation and shader header therefore agree with the truncated chain. |
| E2 | D1 with all ten inter-level `memoryBarrier` calls removed; retain all eleven dispatches. Intentionally incorrect synchronization; timing only. |

“On” in these variants labels the benchmark setting, not functioning occlusion rejection. No previous pyramid or late culling removes draws. The off cases remain unmodified controls.

### D1

| Metric (ms) | Off, no wall | On, no wall | Off, wall | On, wall |
|---|---:|---:|---:|---:|
| Wall clock | 2.400 [2.389, 2.412] | 2.509 [2.498, 2.553] | 1.522 [1.515, 1.527] | 1.599 [1.598, 1.614] |
| Total GPU | 2.329 [2.294, 3.053] | 2.431 [2.422, 2.448] | 1.452 [1.440, 1.469] | 1.523 [1.516, 1.566] |
| Forward | 1.365 [1.336, 1.887] | 1.357 [1.352, 1.360] | 0.374 [0.370, 0.377] | 0.377 [0.374, 0.380] |
| Shadow cascade 3 | 0.368 [0.367, 0.373] | 0.369 [0.368, 0.373] | 0.368 [0.364, 0.373] | 0.372 [0.367, 0.374] |

### D2

| Metric (ms) | Off, no wall | On, no wall | Off, wall | On, wall |
|---|---:|---:|---:|---:|
| Wall clock | 2.399 [2.395, 2.414] | 2.397 [2.396, 2.414] | 1.517 [1.514, 1.520] | 1.514 [1.511, 1.534] |
| Total GPU | 2.336 [2.313, 2.352] | 2.325 [2.317, 2.347] | 1.456 [1.454, 1.460] | 1.444 [1.436, 1.484] |
| Forward | 1.365 [1.354, 1.369] | 1.353 [1.347, 1.367] | 0.375 [0.370, 0.378] | 0.374 [0.371, 0.376] |
| Shadow cascade 3 | 0.368 [0.367, 0.374] | 0.368 [0.366, 0.371] | 0.367 [0.366, 0.371] | 0.372 [0.370, 0.376] |

### E1

| Metric (ms) | Off, no wall | On, no wall | Off, wall | On, wall |
|---|---:|---:|---:|---:|
| Wall clock | 2.405 [2.397, 2.410] | 2.491 [2.479, 2.495] | 1.512 [1.510, 1.522] | 1.588 [1.581, 1.592] |
| Total GPU | 2.330 [2.312, 2.338] | 2.409 [2.406, 2.419] | 1.458 [1.448, 1.472] | 1.522 [1.498, 1.542] |
| Forward | 1.358 [1.350, 1.367] | 1.358 [1.354, 1.364] | 0.375 [0.372, 0.380] | 0.378 [0.374, 0.379] |
| Shadow cascade 3 | 0.371 [0.366, 0.374] | 0.368 [0.367, 0.377] | 0.373 [0.368, 0.375] | 0.373 [0.370, 0.380] |

### E2

| Metric (ms) | Off, no wall | On, no wall | Off, wall | On, wall |
|---|---:|---:|---:|---:|
| Wall clock | 2.403 [2.389, 2.407] | 2.494 [2.478, 2.496] | 1.523 [1.517, 1.526] | 1.591 [1.578, 1.597] |
| Total GPU | 2.330 [2.324, 2.350] | 2.419 [2.384, 2.849] | 1.458 [1.443, 1.466] | 1.518 [1.502, 1.623] |
| Forward | 1.356 [1.353, 1.364] | 1.358 [1.341, 1.378] | 0.375 [0.371, 0.379] | 0.379 [0.376, 0.386] |
| Shadow cascade 3 | 0.368 [0.366, 0.369] | 0.370 [0.365, 0.398] | 0.370 [0.366, 0.371] | 0.371 [0.369, 0.379] |

D1 versus D2 adds 0.112 ms without the wall and 0.085 ms with it (differences of on medians). D2 overlaps its off controls. E1 reduces D1's on median by 0.018/0.011 ms (no wall/wall); E2 by 0.015/0.008 ms. Those changes are small, with some overlapping ranges, and neither variant returns to D2. In particular, removing the ten barriers does not remove the full measured penalty. This is not evidence that unsafe barrier removal is a valid optimization.

## Step 4: base dispatch only

The real no-wall regression triggered this experiment. Starting from D1, omit only the level loop in `recordPyramid`; retain the base dispatch, the surrounding pipeline/image bindings, and the original allocation/header. The unused higher levels are never consumed because D1 disables both pyramid readers. Five normal-order invocations:

| Metric (ms) | Off, no wall | On, no wall | Off, wall | On, wall |
|---|---:|---:|---:|---:|
| Wall clock | 2.396 [2.389, 2.420] | 2.449 [2.439, 2.456] | 1.519 [1.515, 1.523] | 1.559 [1.554, 1.566] |
| Total GPU | 2.336 [2.307, 2.355] | 2.369 [2.357, 2.381] | 1.459 [1.453, 1.475] | 1.496 [1.490, 1.515] |
| Forward | 1.359 [1.348, 1.367] | 1.353 [1.346, 1.368] | 0.375 [0.371, 0.377] | 0.378 [0.376, 0.379] |
| Shadow cascade 3 | 0.371 [0.365, 0.377] | 0.371 [0.366, 0.372] | 0.371 [0.367, 0.377] | 0.373 [0.367, 0.379] |

Base-only on medians are 2.449 ms without the wall and 1.559 ms with it: 0.060/0.040 ms below D1, but 0.052/0.045 ms above D2. Both comparisons have disjoint on ranges. Removing the reduction loop saves measurable time, and executing the base alone still costs measurable time relative to the empty callback. This comparison removes the loop's dispatches, pushes and barriers together; it cannot assign the entire saving to any one of them.

## Conclusion

**Measured:** Release wall-clock throughput shows a small real occlusion overhead without an occluding wall (5.2–5.6%) and a benefit with the wall (12.4–12.5%), consistent in both case orders. The earlier large per-pass slowdown is not a measured end-to-end regression here. D2 removes D1's penalty; base-only sits between them. Truncating the chain or removing its barriers gives only a small improvement.

**Inference and limits:** pyramid execution contributes to the real no-wall cost; the ten barriers alone do not explain the whole cost. The wall-scene timestamp slowdown fails the wall-clock test and is classified as an artefact under that criterion. These measurements do not establish GPU clock changes, tile eviction, bandwidth pressure, Metal encoder scheduling, or a specific MoltenVK timestamp defect as the cause. No frequency telemetry, bandwidth counters, Metal capture, visual equivalence check, or competing-GPU-activity trace was collected. No engine fix is proposed or published.
