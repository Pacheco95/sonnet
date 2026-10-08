# macOS occlusion experiments

Date: 2026-10-08. Report only; no experiment code or local merge is published.

## Conditions and method

- Fresh experiment worktree, starting at `fix/macos-compiler-path` (`2e20d2b`) and merging `feat/occlusion-culling` (`ac533cd`) locally. The full `macos-debug` build passed with Xcode's compiler. Apple Clang 17; MoltenVK 1.4.1; SDK loader 1.4.341; Vulkan validation enabled.
- AC power and `lowpowermode 0` were checked before and after the experiment. The operator confirmed mains power, Low Power Mode off, and pausing other drawing apps. Background GPU activity was not independently profiled.
- One active built-in display: approximately 3024×1964 physical pixels, 1512×982 logical, 120 Hz. All measurements were already built-in-only. There was no external-display configuration to compare, so no display-count effect is claimed.
- Each of eight configurations had one throwaway benchmark, then five counted invocations in normal order; it then had another throwaway and five counted invocations in reverse order. This totals 80 counted invocations plus 16 warm-ups, each containing four cases. Runs and builds were sequential; the clean-main tests ran afterward.
- Normal order: off/no wall, on/no wall, off/wall, on/wall. Reverse order: on/wall, off/wall, on/no wall, off/no wall. Only the four `measure(...)` calls changed between orders.
- Each case retains the benchmark's original 30-frame run. GPU values are the sum of per-pass timestamps for the completed frame reported at the end, not an average of those 30 frames and not an independent whole-command-buffer timer. CPU submitting is the benchmark's median of its last 25 submissions; the tables summarize that figure across five invocations.
- Every table cell is **median [minimum, maximum]**, in milliseconds, over five counted invocations. “—” means that pass was absent. Total GPU medians need not equal the sum of pass medians.
- Only the benchmark ran on altered variants. Its expected indirect-call assertion was adjusted to whether `depth late` was present (12 versus 14 calls), so omitted passes did not abort later cases. Validation assertions stayed enabled. A `WARN` printed `scene.graph.statistics().barrierCount` in all configurations, keeping instrumentation consistent. All 96 invocations completed with four cases and no Vulkan validation errors. No SPIRV-Cross or Metal compilation errors occurred.
- Engine edits were reset between variants. The diagnostic variants intentionally need not produce correct images; neither their visual correctness nor the remedy's pixel equivalence was tested. The previous unmodified occlusion byte-for-byte test result is recorded in the [previous report](https://github.com/Pacheco95/sonnet/blob/mac-validation/occlusion-results/docs/reports/macos-occlusion-validation.md).

Command, from the experiment worktree:

```sh
SDL_VULKAN_LIBRARY=/usr/local/lib/libvulkan.1.dylib   ./build/macos-debug/modules/renderer/renderer_tests   'ten thousand draws and a hundred lights at 1080p'
```

## Configurations

| Label | Local change |
|---|---|
| V0 | Baseline engine code |
| V1 | Keep `depth pyramid`; omit `occlusion cull` and `depth late` |
| V2 | Omit `depth pyramid` and `occlusion cull`; keep `depth late`; force `depthJob->pyramid = 0` |
| V3 | Omit all three new passes; force `depthJob->pyramid = 0`; forward still uses the depth job |
| V4 | Baseline repeat with barrier-count inspection; same executable behavior as V0 |
| D1 | Additional isolation: V1 with `depthJob->pyramid = 0`; pyramid executes but phase one does not read the previous pyramid |
| D2 | Additional isolation: D1 with the `recordPyramid(...)` callback body omitted; retain the pass's depth sampling declaration and its layout transitions |
| R | Only remedy attempted: baseline with pyramid base dimensions halved, through `ensurePyramid(v, glm::max(size / 2u, glm::uvec2{1u}))` |

D1 versus D2 distinguishes executing the pyramid (dispatches, bindings, pushes and internal memory barriers together) from the graph's sampling declaration and transitions alone. It does not distinguish arithmetic, memory traffic, dispatch overhead and internal synchronization from each other.

The remedy is one line in `Renderer::addScenePasses`: at 1920×1080 the base shrinks from 1024×1024 to 512×512 and the chain from 11 to 10 levels. The existing shader still covers the full input depth image in each reduced footprint. No sampler-less-fetch remedy was attempted: `hiz.slang` already uses `depthImage.Load(...)`, not filtered sampling. No second remedy was tried.

## Normal-order measurements

### V0

| Metric (ms) | Off, no wall | On, no wall | Off, wall | On, wall |
|---|---:|---:|---:|---:|
| Total GPU | 2.309 [2.307, 2.326] | 6.190 [3.976, 9.205] | 1.462 [1.448, 2.975] | 5.114 [2.280, 5.149] |
| Depth | 0.150 [0.142, 0.157] | 0.347 [0.232, 0.641] | 0.198 [0.189, 0.352] | 0.135 [0.059, 0.137] |
| Depth pyramid | — | 0.179 [0.130, 0.307] | — | 0.306 [0.143, 0.308] |
| Occlusion cull | — | 0.013 [0.009, 0.022] | — | 0.027 [0.015, 0.028] |
| Depth late | — | 0.004 [0.004, 0.007] | — | 0.008 [0.004, 0.008] |
| Forward | 1.343 [1.326, 1.357] | 3.322 [2.319, 4.990] | 0.381 [0.374, 0.711] | 1.121 [0.483, 1.128] |
| Shadow cascade 3 | 0.371 [0.368, 0.373] | 0.826 [0.565, 1.480] | 0.373 [0.370, 0.813] | 1.460 [0.656, 1.493] |
| CPU submitting | 0.335 [0.331, 0.339] | 0.378 [0.368, 0.401] | 0.317 [0.302, 0.364] | 0.395 [0.356, 0.411] |

### V1

| Metric (ms) | Off, no wall | On, no wall | Off, wall | On, wall |
|---|---:|---:|---:|---:|
| Total GPU | 2.308 [2.299, 2.530] | 5.590 [3.579, 6.738] | 1.456 [1.451, 1.472] | 5.109 [5.073, 7.094] |
| Depth | 0.148 [0.142, 0.159] | 0.347 [0.238, 0.402] | 0.196 [0.190, 0.206] | 0.135 [0.134, 0.665] |
| Depth pyramid | — | 0.180 [0.130, 0.308] | — | 0.310 [0.306, 1.134] |
| Occlusion cull | — | — | — | — |
| Depth late | — | — | — | — |
| Forward | 1.343 [1.328, 1.497] | 3.295 [1.939, 4.170] | 0.380 [0.371, 0.386] | 1.125 [1.119, 1.128] |
| Shadow cascade 3 | 0.373 [0.370, 0.425] | 0.818 [0.564, 0.823] | 0.372 [0.370, 0.374] | 1.479 [1.473, 1.855] |
| CPU submitting | 0.350 [0.341, 0.356] | 0.367 [0.339, 0.375] | 0.321 [0.312, 0.378] | 0.346 [0.332, 0.377] |

### V2

| Metric (ms) | Off, no wall | On, no wall | Off, wall | On, wall |
|---|---:|---:|---:|---:|
| Total GPU | 2.325 [2.306, 2.345] | 2.303 [2.301, 2.363] | 1.452 [1.442, 1.456] | 1.445 [1.433, 1.456] |
| Depth | 0.148 [0.143, 0.153] | 0.150 [0.143, 0.153] | 0.196 [0.186, 0.198] | 0.197 [0.173, 0.198] |
| Depth pyramid | — | — | — | — |
| Occlusion cull | — | — | — | — |
| Depth late | — | 0.004 [0.003, 0.004] | — | 0.003 [0.003, 0.004] |
| Forward | 1.346 [1.334, 1.369] | 1.343 [1.335, 1.349] | 0.373 [0.372, 0.382] | 0.373 [0.370, 0.382] |
| Shadow cascade 3 | 0.372 [0.369, 0.373] | 0.372 [0.369, 0.377] | 0.371 [0.371, 0.376] | 0.375 [0.371, 0.382] |
| CPU submitting | 0.326 [0.315, 0.336] | 0.320 [0.312, 0.340] | 0.303 [0.288, 0.341] | 0.324 [0.316, 0.366] |

### V3

| Metric (ms) | Off, no wall | On, no wall | Off, wall | On, wall |
|---|---:|---:|---:|---:|
| Total GPU | 2.311 [2.301, 2.318] | 2.298 [2.289, 2.316] | 1.470 [1.453, 1.707] | 1.446 [1.431, 1.463] |
| Depth | 0.147 [0.143, 0.155] | 0.153 [0.148, 0.156] | 0.200 [0.194, 0.227] | 0.196 [0.190, 0.200] |
| Depth pyramid | — | — | — | — |
| Occlusion cull | — | — | — | — |
| Depth late | — | — | — | — |
| Forward | 1.347 [1.343, 1.349] | 1.337 [1.322, 1.349] | 0.381 [0.373, 0.436] | 0.373 [0.371, 0.384] |
| Shadow cascade 3 | 0.372 [0.369, 0.374] | 0.370 [0.370, 0.374] | 0.375 [0.372, 0.422] | 0.373 [0.369, 0.379] |
| CPU submitting | 0.338 [0.313, 0.370] | 0.321 [0.303, 0.411] | 0.321 [0.298, 0.340] | 0.316 [0.307, 0.319] |

### V4

| Metric (ms) | Off, no wall | On, no wall | Off, wall | On, wall |
|---|---:|---:|---:|---:|
| Total GPU | 2.309 [2.295, 2.747] | 4.896 [4.227, 7.540] | 1.456 [1.449, 1.465] | 5.128 [5.117, 5.148] |
| Depth | 0.149 [0.143, 0.157] | 0.280 [0.226, 0.340] | 0.198 [0.192, 0.201] | 0.134 [0.134, 0.138] |
| Depth pyramid | — | 0.150 [0.132, 0.182] | — | 0.308 [0.306, 0.312] |
| Occlusion cull | — | 0.011 [0.010, 0.014] | — | 0.028 [0.027, 0.029] |
| Depth late | — | 0.004 [0.004, 0.005] | — | 0.008 [0.007, 0.008] |
| Forward | 1.341 [1.331, 1.503] | 2.802 [2.466, 4.878] | 0.373 [0.372, 0.382] | 1.124 [1.115, 1.127] |
| Shadow cascade 3 | 0.370 [0.368, 0.371] | 0.659 [0.568, 0.825] | 0.372 [0.372, 0.375] | 1.475 [1.468, 1.484] |
| CPU submitting | 0.347 [0.313, 0.361] | 0.381 [0.371, 0.459] | 0.328 [0.307, 0.336] | 0.385 [0.371, 0.410] |

### D1

| Metric (ms) | Off, no wall | On, no wall | Off, wall | On, wall |
|---|---:|---:|---:|---:|
| Total GPU | 2.296 [2.282, 2.324] | 6.080 [3.632, 6.777] | 1.458 [1.440, 1.463] | 6.262 [3.773, 6.449] |
| Depth | 0.150 [0.140, 0.156] | 0.342 [0.214, 0.347] | 0.197 [0.186, 0.201] | 0.761 [0.464, 0.810] |
| Depth pyramid | — | 0.183 [0.120, 0.184] | — | 0.310 [0.176, 0.312] |
| Occlusion cull | — | — | — | — |
| Depth late | — | — | — | — |
| Forward | 1.332 [1.326, 1.339] | 3.339 [2.054, 4.420] | 0.377 [0.373, 0.381] | 1.637 [0.905, 1.655] |
| Shadow cascade 3 | 0.368 [0.366, 0.369] | 0.825 [0.575, 1.088] | 0.373 [0.372, 0.377] | 1.474 [0.832, 1.490] |
| CPU submitting | 0.365 [0.349, 0.368] | 0.366 [0.356, 0.390] | 0.332 [0.323, 0.353] | 0.369 [0.365, 0.388] |

### D2

| Metric (ms) | Off, no wall | On, no wall | Off, wall | On, wall |
|---|---:|---:|---:|---:|
| Total GPU | 2.312 [2.292, 2.319] | 2.301 [2.278, 2.303] | 1.457 [1.450, 1.470] | 1.442 [1.437, 1.446] |
| Depth | 0.151 [0.146, 0.155] | 0.152 [0.144, 0.157] | 0.198 [0.197, 0.205] | 0.197 [0.196, 0.200] |
| Depth pyramid | — | 0.002 [0.002, 0.002] | — | 0.002 [0.002, 0.002] |
| Occlusion cull | — | — | — | — |
| Depth late | — | — | — | — |
| Forward | 1.339 [1.332, 1.346] | 1.340 [1.327, 1.346] | 0.373 [0.371, 0.377] | 0.379 [0.372, 0.379] |
| Shadow cascade 3 | 0.369 [0.369, 0.370] | 0.370 [0.368, 0.376] | 0.374 [0.371, 0.376] | 0.370 [0.370, 0.375] |
| CPU submitting | 0.326 [0.312, 0.348] | 0.327 [0.300, 0.342] | 0.317 [0.294, 0.360] | 0.326 [0.301, 0.372] |

### R — half-size pyramid

| Metric (ms) | Off, no wall | On, no wall | Off, wall | On, wall |
|---|---:|---:|---:|---:|
| Total GPU | 2.306 [2.295, 2.320] | 5.626 [3.645, 9.281] | 1.450 [1.443, 1.461] | 5.111 [5.081, 5.717] |
| Depth | 0.153 [0.147, 0.163] | 0.340 [0.228, 0.599] | 0.195 [0.191, 0.198] | 0.136 [0.135, 0.139] |
| Depth pyramid | — | 0.164 [0.118, 0.276] | — | 0.268 [0.263, 0.281] |
| Occlusion cull | — | 0.014 [0.010, 0.022] | — | 0.029 [0.028, 0.030] |
| Depth late | — | 0.005 [0.005, 0.008] | — | 0.008 [0.008, 0.009] |
| Forward | 1.339 [1.327, 1.350] | 3.262 [2.059, 5.967] | 0.376 [0.374, 0.381] | 1.118 [1.108, 1.131] |
| Shadow cascade 3 | 0.369 [0.364, 0.373] | 0.827 [0.561, 1.470] | 0.373 [0.368, 0.374] | 1.489 [1.477, 1.493] |
| CPU submitting | 0.343 [0.325, 0.426] | 0.388 [0.360, 0.434] | 0.318 [0.305, 0.348] | 0.388 [0.366, 0.393] |

## Reversed-order measurements

Columns retain the same meaning as above; execution order was reversed.

### V0 reversed

| Metric (ms) | Off, no wall | On, no wall | Off, wall | On, wall |
|---|---:|---:|---:|---:|
| Total GPU | 2.307 [2.283, 2.311] | 6.284 [3.705, 7.475] | 1.453 [1.434, 1.463] | 5.137 [5.113, 5.254] |
| Depth | 0.148 [0.146, 0.157] | 0.332 [0.239, 0.573] | 0.199 [0.176, 0.201] | 0.134 [0.134, 0.135] |
| Depth pyramid | — | 0.178 [0.132, 0.184] | — | 0.310 [0.309, 0.312] |
| Occlusion cull | — | 0.013 [0.010, 0.014] | — | 0.029 [0.028, 0.030] |
| Depth late | — | 0.004 [0.004, 0.005] | — | 0.007 [0.007, 0.008] |
| Forward | 1.337 [1.320, 1.344] | 3.264 [2.012, 4.439] | 0.378 [0.374, 0.383] | 1.119 [1.112, 1.129] |
| Shadow cascade 3 | 0.370 [0.367, 0.373] | 0.813 [0.565, 1.447] | 0.375 [0.372, 0.375] | 1.475 [1.465, 1.493] |
| CPU submitting | 0.327 [0.301, 0.382] | 0.379 [0.361, 0.409] | 0.320 [0.304, 0.395] | 0.400 [0.375, 0.407] |

### V1 reversed

| Metric (ms) | Off, no wall | On, no wall | Off, wall | On, wall |
|---|---:|---:|---:|---:|
| Total GPU | 2.305 [2.296, 2.329] | 4.893 [3.828, 7.894] | 1.453 [1.442, 1.458] | 5.082 [5.039, 5.090] |
| Depth | 0.154 [0.151, 0.161] | 0.260 [0.231, 0.597] | 0.196 [0.192, 0.199] | 0.134 [0.133, 0.135] |
| Depth pyramid | — | 0.148 [0.133, 0.311] | — | 0.309 [0.307, 0.312] |
| Occlusion cull | — | — | — | — |
| Depth late | — | — | — | — |
| Forward | 1.336 [1.327, 1.348] | 2.739 [2.175, 4.363] | 0.373 [0.372, 0.378] | 1.119 [1.111, 1.132] |
| Shadow cascade 3 | 0.369 [0.369, 0.376] | 0.780 [0.572, 1.473] | 0.374 [0.369, 0.375] | 1.470 [1.460, 1.474] |
| CPU submitting | 0.291 [0.275, 0.306] | 0.328 [0.310, 0.342] | 0.294 [0.273, 0.335] | 0.345 [0.329, 0.356] |

### V2 reversed

| Metric (ms) | Off, no wall | On, no wall | Off, wall | On, wall |
|---|---:|---:|---:|---:|
| Total GPU | 2.308 [2.297, 2.317] | 2.302 [2.290, 2.312] | 1.469 [1.450, 2.463] | 1.442 [1.428, 5.432] |
| Depth | 0.149 [0.142, 0.152] | 0.147 [0.145, 0.156] | 0.199 [0.195, 0.347] | 0.197 [0.184, 0.812] |
| Depth pyramid | — | — | — | — |
| Occlusion cull | — | — | — | — |
| Depth late | — | 0.004 [0.003, 0.004] | — | 0.003 [0.003, 0.008] |
| Forward | 1.338 [1.335, 1.342] | 1.335 [1.333, 1.349] | 0.380 [0.375, 0.706] | 0.375 [0.370, 1.358] |
| Shadow cascade 3 | 0.370 [0.368, 0.375] | 0.368 [0.367, 0.369] | 0.374 [0.371, 0.576] | 0.373 [0.368, 1.463] |
| CPU submitting | 0.320 [0.311, 0.339] | 0.333 [0.315, 0.378] | 0.320 [0.304, 0.366] | 0.342 [0.316, 0.357] |

### V3 reversed

| Metric (ms) | Off, no wall | On, no wall | Off, wall | On, wall |
|---|---:|---:|---:|---:|
| Total GPU | 2.308 [2.278, 2.313] | 2.301 [2.298, 2.311] | 1.454 [1.450, 1.462] | 1.455 [1.443, 1.461] |
| Depth | 0.149 [0.144, 0.157] | 0.154 [0.150, 0.158] | 0.198 [0.195, 0.204] | 0.200 [0.198, 0.203] |
| Depth pyramid | — | — | — | — |
| Occlusion cull | — | — | — | — |
| Depth late | — | — | — | — |
| Forward | 1.331 [1.321, 1.337] | 1.337 [1.334, 1.347] | 0.374 [0.370, 0.376] | 0.380 [0.373, 0.382] |
| Shadow cascade 3 | 0.371 [0.369, 0.377] | 0.369 [0.369, 0.373] | 0.371 [0.370, 0.378] | 0.373 [0.373, 0.374] |
| CPU submitting | 0.338 [0.307, 0.344] | 0.339 [0.308, 0.349] | 0.329 [0.315, 0.397] | 0.344 [0.336, 0.364] |

### V4 reversed

| Metric (ms) | Off, no wall | On, no wall | Off, wall | On, wall |
|---|---:|---:|---:|---:|
| Total GPU | 2.300 [2.284, 2.478] | 4.437 [3.531, 7.640] | 1.451 [1.441, 1.464] | 5.124 [2.868, 5.141] |
| Depth | 0.150 [0.143, 0.154] | 0.267 [0.212, 0.594] | 0.185 [0.182, 0.202] | 0.134 [0.075, 0.137] |
| Depth pyramid | — | 0.146 [0.121, 0.309] | — | 0.310 [0.176, 0.313] |
| Occlusion cull | — | 0.011 [0.009, 0.022] | — | 0.029 [0.017, 0.032] |
| Depth late | — | 0.004 [0.004, 0.007] | — | 0.007 [0.004, 0.008] |
| Forward | 1.334 [1.326, 1.528] | 2.557 [1.948, 3.751] | 0.380 [0.372, 0.383] | 1.119 [0.617, 1.120] |
| Shadow cascade 3 | 0.371 [0.369, 0.372] | 0.665 [0.561, 1.450] | 0.376 [0.373, 0.376] | 1.475 [0.828, 1.482] |
| CPU submitting | 0.324 [0.320, 0.341] | 0.385 [0.357, 0.403] | 0.329 [0.314, 0.343] | 0.416 [0.402, 0.422] |

### D1 reversed

| Metric (ms) | Off, no wall | On, no wall | Off, wall | On, wall |
|---|---:|---:|---:|---:|
| Total GPU | 2.306 [2.270, 2.327] | 5.583 [3.572, 7.368] | 1.455 [1.450, 1.466] | 6.297 [6.259, 6.346] |
| Depth | 0.151 [0.150, 0.156] | 0.333 [0.241, 0.637] | 0.197 [0.188, 0.202] | 0.801 [0.746, 0.836] |
| Depth pyramid | — | 0.180 [0.135, 0.314] | — | 0.310 [0.307, 0.311] |
| Occlusion cull | — | — | — | — |
| Depth late | — | — | — | — |
| Forward | 1.338 [1.335, 1.364] | 3.240 [1.978, 3.400] | 0.377 [0.373, 0.381] | 1.635 [1.631, 1.643] |
| Shadow cascade 3 | 0.370 [0.367, 0.371] | 0.819 [0.566, 1.457] | 0.374 [0.372, 0.380] | 1.480 [1.474, 1.483] |
| CPU submitting | 0.336 [0.310, 0.356] | 0.377 [0.363, 0.383] | 0.336 [0.320, 0.354] | 0.384 [0.370, 0.392] |

### D2 reversed

| Metric (ms) | Off, no wall | On, no wall | Off, wall | On, wall |
|---|---:|---:|---:|---:|
| Total GPU | 2.311 [2.284, 2.319] | 2.303 [2.289, 2.319] | 1.450 [1.448, 1.466] | 1.454 [1.446, 1.458] |
| Depth | 0.151 [0.145, 0.159] | 0.153 [0.144, 0.161] | 0.198 [0.196, 0.201] | 0.199 [0.195, 0.202] |
| Depth pyramid | — | 0.002 [0.002, 0.002] | — | 0.002 [0.002, 0.002] |
| Occlusion cull | — | — | — | — |
| Depth late | — | — | — | — |
| Forward | 1.335 [1.330, 1.343] | 1.339 [1.330, 1.349] | 0.377 [0.372, 0.383] | 0.378 [0.371, 0.385] |
| Shadow cascade 3 | 0.370 [0.368, 0.374] | 0.371 [0.367, 0.371] | 0.375 [0.368, 0.376] | 0.373 [0.370, 0.378] |
| CPU submitting | 0.324 [0.307, 0.343] | 0.334 [0.317, 0.365] | 0.321 [0.310, 0.386] | 0.349 [0.333, 0.374] |

### R — half-size pyramid, reversed

| Metric (ms) | Off, no wall | On, no wall | Off, wall | On, wall |
|---|---:|---:|---:|---:|
| Total GPU | 2.308 [2.302, 2.312] | 7.491 [5.908, 9.036] | 1.533 [1.451, 2.278] | 5.080 [4.895, 5.133] |
| Depth | 0.147 [0.144, 0.152] | 0.357 [0.332, 0.674] | 0.211 [0.198, 0.309] | 0.135 [0.133, 0.135] |
| Depth pyramid | — | 0.186 [0.163, 0.335] | — | 0.267 [0.262, 0.269] |
| Occlusion cull | — | 0.013 [0.013, 0.023] | — | 0.029 [0.028, 0.030] |
| Depth late | — | 0.006 [0.005, 0.010] | — | 0.008 [0.008, 0.010] |
| Forward | 1.344 [1.331, 1.353] | 4.407 [3.254, 5.122] | 0.396 [0.378, 0.614] | 1.126 [1.115, 1.130] |
| Shadow cascade 3 | 0.368 [0.368, 0.371] | 0.826 [0.819, 1.901] | 0.391 [0.372, 0.569] | 1.478 [1.462, 1.496] |
| CPU submitting | 0.333 [0.304, 0.352] | 0.406 [0.371, 0.462] | 0.349 [0.293, 0.406] | 0.405 [0.387, 0.463] |

## Counts and depth transitions

Counts were identical across all five runs in both orders for each row below. Off always had 5396/10000 survivors without the wall, 7344/10001 with it, and 38 graph image barriers.

| Configuration | On, no wall survivors | On, wall survivors | On, graph image barriers |
|---|---:|---:|---:|
| V0 | 5396/10000 | 61/10001 | 40 |
| V1 | 5396/10000 | 61/10001 | 39 |
| V2 | 5396/10000 | 7344/10001 | 39 |
| V3 | 5396/10000 | 7344/10001 | 38 |
| V4 | 5396/10000 | 61/10001 | 40 |
| D1 | 5396/10000 | 7344/10001 | 39 |
| D2 | 5396/10000 | 7344/10001 | 39 |
| R | 5396/10000 | 61/10001 | 40 |

In `RenderGraph.cpp`, `requirementFor(ImageAccess::DepthAttachment)` requests `DepthAttachment` layout with early/late fragment-test stages and depth read/write access. `SampledCompute` requests `ShaderReadOnly` layout with compute-shader stage and shader-read access. `emitBarriers` emits that change before `depth pyramid`; `depth late` requests the depth-attachment layout again. V1 returns to depth-attachment layout at forward instead. V2 keeps depth-attachment layout but adds a write-after-write barrier for the second depth pass. D2 retains the transitions without executing pyramid commands, and remains fast.

The counts are `GraphStatistics::barrierCount`, which counts graph image barriers, not the explicit `commands.memoryBarrier(...)` calls inside renderer callbacks. The original 11-level pyramid has ten compute-to-compute memory barriers between levels; these are outside the graph counter. Halving the base removes one level and one such barrier. The printed graph count belongs to the final recorded frame, including readback; the reported GPU timestamps are from an earlier completed frame. Neither counter proves physical tile-memory residency. V4 is the requested inspection/repeat, not an implementation that keeps sampled depth resident.

## Order, previous result, and remedy effect

- The original report's single samples were 3.896 versus 10.099 ms without the wall and 2.649 versus 5.161 ms with it. Current V0 normal-order medians are 2.309 versus 6.190 ms and 1.462 versus 5.114 ms. Thus the slowdown reproduces, but those old absolute numbers are not stable performance estimates.
- Reversing V0 does not remove the result: no-wall on is 6.284 ms versus 6.190 normal; wall on is 5.137 versus 5.114 ms. V4, a later baseline repeat, has lower no-wall medians (4.896 normal, 4.437 reversed), within the broad observed ranges. There is no supported claim that order has zero effect, but the slowdown is not explained solely by the first case's position. Five last-frame samples remain noisy.
- V1 stays slow, so the late-cull compute and the extra late depth render pass are not required to reproduce it. V2's split alone and V3's depth-command reuse match their occlusion-off controls in their medians. In particular, V3 forward is not consistently slower than occlusion off.
- D1 still slows down with no previous-frame test. D2 keeps the same graph transitions but returns to approximately off timings. The tested difference is execution of `recordPyramid`, including its internal synchronization.
- The only remedy did not consistently improve total GPU time. Without the wall, R is 5.626 ms normal and 7.491 reversed, versus V0's 6.190 and 6.284; these overlap the wide baseline range. With the wall, R is 5.111 and 5.080, versus V0's 5.114 and 5.137: no material median improvement. The remedy's pyramid pass is cheaper, but the frame slowdown remains. Survivor counts stay 5396 without the wall and 61 with it; this is not proof of pixel equivalence.
- Single-display comparison: all runs used the built-in display only, so no second display configuration or effect was measured.

## Part C: validation on clean main

Clean detached checkout of `origin/main` at `beb204f`, with no branches merged and no source changes. Configured `macos-debug` with explicit Xcode `CC` and `CXX` paths to avoid the known `/usr/bin` header-shadowing issue without applying a patch. Built `runtime_tests` and `editor_tests`, then ran:

```sh
SDL_VULKAN_LIBRARY=/usr/local/lib/libvulkan.1.dylib   ctest --preset macos-debug -R 'runtime_tests|editor_tests' --output-on-failure
```

Both suites passed, with no skipped cases. The passing CTest console output hides successful-suite logs, so counts came from `build/macos-debug/Testing/Temporary/LastTest.log`.

| Suite | VUID-vkBeginCommandBuffer-commandBuffer-00050 count |
|---|---:|
| runtime_tests | 6 |
| editor_tests | 5 |
| Total | 11 |

These messages also occur on clean main, in exactly the previously observed counts. No other VUID was found in that run. This does not diagnose the pool lifecycle error.

Full message text, with handles redacted:

```text
[15:00:47.057] [error] [rhi] [VulkanDevice.cpp:1327] VUID-vkBeginCommandBuffer-commandBuffer-00050 [frame 0 upload pool]: vkBeginCommandBuffer(): VkCommandBuffer <device> attempts to implicitly reset cmdBuffer created from VkCommandPool <device>[frame 0 upload pool] that does NOT have the VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT bit set.
The Vulkan spec states: If commandBuffer was allocated from a VkCommandPool which did not have the VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT flag set, commandBuffer must be in the initial state (https://vulkan.lunarg.com/doc/view/1.4.341.0/mac/antora/spec/latest/chapters/cmdbuffers.html#VUID-vkBeginCommandBuffer-commandBuffer-00050)
```

## Conclusion

**Measured:** the slowdown survives removal of late culling, the late depth pass, and the previous-frame test. It disappears when the pyramid callback is removed while its graph transitions remain. Splitting depth alone and using the depth job for forward do not reproduce it. Reducing pyramid resolution is not an effective remedy in these samples. The validation message is also present on clean main.

**Inference and limits:** the responsible path is pyramid execution and/or its internal synchronization, not simply the extra render pass, graph layout-transition count, or forward command reuse. The explicit pyramid time is too small to explain the entire difference by adding its duration alone; even shadow cascade 3, before the pyramid in each frame, is slower across the repeated-frame workload. These tests do not identify the lower-level reason for that broader effect. No GPU-frequency telemetry, Metal capture, bandwidth counters, or independent whole-command-buffer timing was collected, so this report does not assign the residual effect to clock changes, tile eviction, a driver scheduling issue, or timestamp behavior. Separating pyramid dispatch work from its internal barriers would require another experiment; no engine fix is established here.
