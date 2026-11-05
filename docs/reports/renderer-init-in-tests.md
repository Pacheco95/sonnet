# Renderer initialization in the integration tests

Measurements for [issue #470](https://github.com/Pacheco95/sonnet/issues/470), on 2026-11-05. Most `editor_tests` and `runtime_tests` cases make their own window, Vulkan device, editor or game and renderer, and `VulkanDevice` passed no `VkPipelineCache` to pipeline creation. The question was which of those repeated costs are large enough to matter on Lavapipe, CI's renderer, before choosing anything to change. With the suites' shards not adopted ([Editor shards](parallel-test-suites.md#editor-shards)), making `editor_tests` itself cheaper was what was left to shorten CI's Test step.

## Method

- A Debug build with GCC 14 (`linux-debug`) and an ASan build with Clang 20 (`linux-asan`), on Lavapipe (Mesa 25.2.8, LLVM 20.1.2), on an 8-core, 16-thread desktop limited to four logical CPUs with `taskset -c 0-3`, the vCPU count of a Linux runner, with the power profile fixed at `performance`.
- Without the validation layer. CI's Linux runners have none (their logs say `validation requested but VK_LAYER_KHRONOS_validation is not installed`), so the local runs disabled it with `VK_LOADER_LAYERS_DISABLE='~explicit~'`. With it, creating a device took about four times as long (84 ms against 18 ms), a cost CI does not pay.
- The breakdown comes from temporary scoped timers, not committed: around window, device and swapchain creation, the editor's and the game's constructors, `Renderer::createPipelines` and each `vkCreate*Pipelines` call, opening a project or a bundle and waiting for its loads, the frame (`update`, `render`, `afterPresent`, then `beginFrame` and `endFrame`, which on Lavapipe is where the CPU rasterises, presents and waits) and the destruction of the editor or game and of the device. A Catch2 listener summed them per test case. Each suite's binary ran on its own, with the Mesa shader cache warm unless a row says otherwise.
- Before and after timings are of builds without the timers: each suite's binary alone, three runs, and the command CI runs, `ctest --preset <preset> --parallel 2 --timeout 300`, two runs.

## Where the time goes

Seconds summed over the cases, and their share of the suite's case time. 277 cases in `editor_tests`, 48 in `runtime_tests` (CI's reports count 321 and 58 entries, a case's sections each being one); 120 and 23 devices, 169 and 34 editors or games, 7820 and 1551 pipelines.

| Part | `editor_tests` Debug | `runtime_tests` Debug | `editor_tests` ASan | `runtime_tests` ASan |
|---|--:|--:|--:|--:|
| Window, device and swapchain creation | 2.3 (3%) | 0.4 (1%) | 9.7 (7%) | 1.1 (3%) |
| Editor or game construction, pipelines aside | 3.5 (4%) | 0.4 (1%) | 7.3 (5%) | 0.6 (1%) |
| **Pipeline creation** | **15.0 (19%)** | **2.9 (10%)** | **35.2 (26%)** | **6.2 (14%)** |
| Asset preparation on the main thread | 0.1 (0%) | 0.3 (1%) | 0.1 (0%) | 0.7 (2%) |
| Frames: update and record | 2.8 (3%) | 2.2 (7%) | 7.8 (6%) | 5.3 (12%) |
| Frames: submit, rasterise, present, wait | 51.3 (64%) | 21.5 (70%) | 60.4 (45%) | 23.4 (55%) |
| Teardown of the editor or game and the device | 1.4 (2%) | 0.3 (1%) | 4.1 (3%) | 1.5 (4%) |
| The test bodies themselves | 4.1 (5%) | 2.9 (9%) | 9.7 (7%) | 4.0 (9%) |
| **Cases** | **80.5** | **30.9** | **134.4** | **42.9** |

- **Frames** are most of both suites, and they are the work the tests exist to check: Lavapipe draws on the CPU. A device's first two frames cost more than the rest (44 ms against 18 ms in Debug `editor_tests`, 202 such pairs), because llvmpipe compiles a shader variant for the state it is first drawn with. That is about 5 s of `editor_tests`, and the Mesa disk cache already serves it (below).
- **Pipeline creation is the largest repeated cost that is not the tests' own work**: 19% of `editor_tests` and 10% of `runtime_tests` in Debug, 26% and 14% under ASan. Each renderer creates its 47 engine pipelines (fewer in a few test configurations) in about 90 ms, one after another. Lavapipe translates and optimises the SPIR-V on each `vkCreate*Pipelines` call; the four `forward` variants take 13 ms each, half of the total, and every other pipeline 0.1 to 4 ms. The shader modules themselves cost 0.1 s in the whole suite.
- **Asset preparation is negligible**: opening the samples and importing their few textures and glTF files takes under 1 s per suite, and copying the samples (`sonnet::test::copySample`) a few milliseconds. Device creation is 2 to 7%, construction other than pipelines (Dear ImGui, the fonts, Jolt, Lua, the audio device) 4 to 5%, teardown 2 to 4%.

## Caching

**The Mesa shader cache is already on and already does what it can.** For one runtime case, a game with a cooked bundle and 18 frames: 903 ms with an empty cache, 589 ms warm, 1072 ms with `MESA_SHADER_CACHE_DISABLE=true`. Almost all of that difference is in the frames (690, 416 and 874 ms), the draw-time compiles; pipeline creation was 101, 94 and 101 ms. Over a suite a cold cache costs little, because only the first case to draw a state misses: `editor_tests` took 83.3 s cold and 80.5 s warm. A CI job starts with an empty cache in its home directory and its suites fill it as they go; the first `ctest` run below, with a cold cache, took 118.8 s against 104.5 s warm.

**A `VkPipelineCache` cannot help on Lavapipe.** Tried with a cache per device whose data was carried from one device to the next within the process: `vkGetPipelineCacheData` returned 32 bytes, the header alone, and a case that builds 141 pipelines on one device through three renderers took 274 ms without the cache and 273 ms with it, even for the second and third renderer on the same device. Lavapipe keeps nothing in it, so a cache saved to disk would have nothing to save; NVIDIA and the other desktop drivers keep a cache of their own across processes. The experiment was not kept, and no cache, no invalidation and no cold or warm behaviour of one were adopted.

**Nothing immutable is worth sharing between cases either.** The SPIR-V files and shader modules are 0.1 s of `editor_tests`; the samples' imports are under 1 s. Reusing a device or a renderer from one case to the next would remove device creation and pipelines, but it would carry mutable state (the renderer's meshes, textures, histories and particles, the device's pools) from one case into the next and drop the per-case construction and destruction coverage, so it was not considered further.

## What changed

The pipelines are built side by side. `IDevice::createPipelines` takes a list of graphics and compute pipeline descriptions and a job system; `VulkanDevice` fills every create info on its own thread, makes each `vkCreate*Pipelines` call one job, runs them on the job system's workers and on the calling thread while it waits, and adopts the results back on its own thread ([rendering.md](../rendering.md#frame-structure)). The device is still only called on the thread that owns it. `Renderer` creates its 17 shader modules and then all its pipelines in one call on `RendererSettings::jobs`, which the editor and the game set, and a shader reload rebuilds a module's pipelines the same way. A null job system builds them one after another, as before, which is what `renderer_tests` and `sonnet_cook` get.

The same pipelines are made from the same create infos, so the images do not change. The golden captures, their tolerances, the simulation steps and the validation and sanitizer checks are untouched, and every case still makes and destroys its own device, editor or game and renderer.

With the change, a renderer's pipelines take 31 ms instead of 86 ms in Debug `runtime_tests` and 32 ms instead of 89 ms in `editor_tests` (wall time), 79 ms instead of 208 ms under ASan:

| Part | `editor_tests` Debug | `runtime_tests` Debug | `editor_tests` ASan | `runtime_tests` ASan |
|---|--:|--:|--:|--:|
| Pipeline creation, before | 15.0 | 2.9 | 35.2 | 6.2 |
| Pipeline creation, after | 5.4 | 1.0 | 13.5 | 2.3 |
| Cases, before | 80.5 | 30.9 | 134.4 | 42.9 |
| Cases, after | 70.2 | 30.2 | 112.9 | 39.0 |

The driver does not do less work: the jobs' own durations add up to more than before (25.4 s against 14.8 s in Debug `editor_tests`), because the job system sizes itself from `hardware_concurrency()`, which `taskset` does not change, so 15 workers shared four CPUs here. A runner with four vCPUs gets three workers. On a CPU that is already busy, as with a second suite beside it, the gain is smaller; the `ctest --parallel 2` runs below include that.

## Results

Wall time in seconds, builds without the timers. Every run passed, 15 of 15 tests.

| Run | Before | After | Change |
|---|---|---|--:|
| `editor_tests` alone, Debug | 80.8, 80.7, 85.9 | 70.2, 70.6, 72.8 | −13% |
| `runtime_tests` alone, Debug | 32.2, 32.7, 32.1 | 30.4, 30.2, 29.6 | −6% |
| `editor_tests` alone, ASan | 136.9, 135.6, 141.5 | 112.0, 117.3, 112.0 | −18% |
| `runtime_tests` alone, ASan | 42.9, 43.2, 44.3 | 38.8, 39.2, 40.6 | −9% |
| `ctest --parallel 2`, Debug | 118.8 (cold Mesa cache), 104.5 | 96.0, 96.7 | −8% (warm) |
| `ctest --parallel 2`, ASan | 170.9 (cold Mesa cache), 166.3 | 145.2, 144.8 | −13% (warm) |

In the `ctest` runs the Test step is still `editor_tests` (96.0 and 96.7 s Debug, 145.1 and 144.8 s ASan, from 104.4 and 166.3 s), with `runtime_tests` from 43.0 to 36.3 and 39.1 s in Debug and 53.1 to 46.6 and 46.3 s under ASan. `renderer_tests`, which builds its renderers without a job system, stayed where it was (14.8 to 15.2 and 14.9 s Debug, 34.7 to 33.4 and 34.4 s ASan) and is the control.

The complete suites also passed with the validation layer and its synchronization validation on, in Debug and under ASan, which is how a developer machine with the Vulkan SDK runs them: `rhi_tests` and every suite above it fail on any validation message.

## In CI

Measured on 2026-11-05 in [run 42065940685](https://github.com/Pacheco95/sonnet/actions/runs/42065940685) (attempt 2, then each Linux job re-run once), against [run 41930016334](https://github.com/Pacheco95/sonnet/actions/runs/41930016334). CI's runners use a newer Mesa (26.2.6 with LLVM 21.1.8, from the kisak PPA) than the measuring machine. Runner speed moves a whole job by up to 40% between runs, so the comparison is per case: for each suite, the median over its cases of the new time divided by the baseline time, from the jobs' timing artifacts ([Test timings](../build.md#test-timings)), with `renderer_tests` and `assets_tests`, which this change does not touch, as the control.

| Job | Run | Test step, s (baseline) | `editor_tests` | `runtime_tests` | `renderer_tests` | `assets_tests` | `editor_tests` against the controls |
|---|---|--:|--:|--:|--:|--:|--:|
| Linux Clang | first | 368 (364) | 0.91 | 0.88 | 1.01 | 0.97 | 0.92 |
| Linux Clang | re-run | 338 (364) | 0.90 | 1.00 | 0.97 | 1.00 | 0.91 |
| Linux ASan | first | 319 (361) | 0.84 | 0.80 | 1.02 | 0.96 | 0.85 |
| Linux ASan | re-run | 258 (361) | 0.72 | 0.61 | 0.81 | 0.75 | 0.92 |
| Linux GCC | first | 253 (196) | 1.01 | 1.32 | 1.32 | 1.00 | 0.87 |
| Linux GCC | re-run | 327 (196) | 1.27 | 1.55 | 1.80 | 1.44 | 0.78 |

The last column is `editor_tests`' median divided by the mean of the two controls' medians.

- **`editor_tests`** was 8 to 22% faster than the controls in all six jobs, in line with the local 8 to 18%.
- **`runtime_tests`** was faster than the controls in four of the six jobs (Clang 0.89 and 1.02, ASan 0.81 and 0.78, GCC 1.14 and 0.96 against the controls). Its gain is close to the noise.
- **The Test steps** mostly follow the runner: the controls alone ranged from 0.75 to 1.80. The GCC baseline (196 s) was already a fast runner in [#468](parallel-test-suites.md#measured-in-ci), so GCC can be read only against its own controls. In Linux Clang's first run, a few long cases that mostly draw frames moved by 14 to 20 s each in both directions (68 s gained, 73 s lost), which kept that suite's total flat while its typical case was 9% faster.
- Every job passed with the same entries as the baseline: 321 in `editor_tests`, 58 in `runtime_tests`. Windows MSVC took 58 s against 59 s. The Windows and macOS runners have no Vulkan device for these suites.

The measurement logs, the timer patch and the scripts are in `~/sonnet-measurements/470-renderer-init/` on the measuring machine, with the CI comparison in its `ci-42065940685/` folder.

## What is left

- Frames are 64 to 75% of both suites, and most of that is the drawing the tests check. A test that draws more frames than its assertion needs is the next place to look, case by case, but that changes what a test exercises and is for the test's owner to decide.
- The first frames after a new device recompile llvmpipe's shader variants from the Mesa cache, about 5 s of Debug `editor_tests`; only fewer devices would remove it.
- Device creation is 7% of `editor_tests` under ASan (78 ms a device against 18 ms in Debug), Lavapipe's own start-up slowed by ASan's allocator.
