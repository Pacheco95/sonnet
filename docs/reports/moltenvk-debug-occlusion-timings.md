# Debug occlusion timings on MoltenVK

Investigation of [issue 141](https://github.com/Pacheco95/sonnet/issues/141),
2026-10-20, starting from `5c1d913`. Apple M4 Max, macOS 26.7.1,
Apple Clang 17, SDK MoltenVK 1.4.1 / Vulkan 1.4.334 / loader 1.4.341,
SDL 3.4.16. All runs below were sequential on the same machine.

## Finding

Sparse CPU submission changes the GPU's execution time through power management.
The large Debug pass times are real elapsed GPU intervals, independently visible
in a Metal System Trace, but are not comparable measurements of occlusion's added
work at the sustained Release workload. They are not explained by validation or
Debug shaders alone. In particular, passes before the depth pyramid grow too.

A Release control with a 26 ms sleep before each `GpuScene::render` frame also
produces large GPU times. Removing that sleep restores the original timings.
The trace records predominantly minimum GPU performance state during the Debug
occlusion cases. These observations establish submission pacing as a source of
the inflation and support power management as its mechanism; they do not identify
the driver's exact frequency policy or guarantee identical clocks between cases.
There is no evidence here that the engine adds several milliseconds of occlusion
work in the sustained Release workload. Keep Release wall clock as the performance
reference. Do not subtract Debug pass samples across configurations to estimate
occlusion cost.

The [performance-state follow-up](apple-gpu-performance-state.md) repeats the live
benchmark with a recording-scoped Maximum state request and checks observed
device-state intervals. It records the supported workflow and its limits.

## Controlled measurements

The hidden `ten thousand draws and a hundred lights at 1080p` benchmark uses AA
None, 30 warmup frames and 120 timed frames per case, including the final
wait/readback. GPU sums are one earlier frame's available pass intervals, not an
average over the 120 frames. Each table cell is wall clock / sampled GPU sum in ms.
Single runs establish mechanisms, not a statistical performance guarantee.

| Configuration | Off, no wall | On, no wall | Off, wall | On, wall |
|---|---:|---:|---:|---:|
| Release | 2.611 / 2.545 | 2.754 / 2.690 | 1.719 / 1.657 | 1.515 / 1.457 |
| Debug, validation on | 28.116 / 3.260 | 28.257 / 11.264 | 27.742 / 2.131 | 28.502 / 6.008 |
| Debug, validation off | 27.457 / 2.810 | 27.683 / 11.244 | 27.340 / 2.628 | 27.648 / 6.026 |
| Debug, Release shaders, validation on | 28.827 / 3.213 | 29.614 / 10.440 | 28.393 / 2.809 | 28.500 / 6.107 |
| Release, 26 ms sleep per frame | 32.917 / 11.151 | 35.845 / 11.440 | 34.174 / 7.160 | 35.743 / 6.261 |
| Release, sleep removed | 2.643 / 2.594 | 2.760 / 2.691 | 1.710 / 1.656 | 1.512 / 1.448 |

Every run passed all 16 benchmark assertions, including validation-message and
indirect-call counts. The validation-off control changes only the test device's
`DeviceDesc::enableValidation` to false. The shader control copies Release SPIR-V
into the Debug test's shader directory, leaving the Debug executable, libraries
and validation intact. The sleep control changes only the test harness: include
`<thread>` and insert `std::this_thread::sleep_for(std::chrono::milliseconds{26});`
at the top of `GpuScene::render`'s frame loop, before `beginFrame`. None of those
diagnostic changes is retained.

For the ordinary Debug run, shadow cascade 3 grows from 0.508 to 1.593 ms and
forward from 1.903 to 6.692 ms without a wall. Both passes already exist with
occlusion off; the shadow pass precedes the pyramid. The depth pyramid itself is
0.307 ms in that sample. CPU pass recording is 0.720 / 0.904 / 0.638 / 0.898 ms
across the four cases; median submission is 0.309 / 0.362 / 0.278 / 0.387 ms.
Those numbers cover only pass recording and `endFrame`, not the full CPU frame.
In Release the initial run's wall clock changes by +5.5% without a wall and
-11.9% with a wall; the repeat changes by +4.4% and -11.6%.

## Independent Metal evidence

A separate Debug run with validation on was launched through Xcode 26's
`Metal System Trace`, using its default GPU performance setting. Its 18.832 s
recording contains 600 benchmark command buffers: four cases of 150 frames.
Export `metal-gpu-intervals` and group rows for `renderer_tests` by command-buffer
id; each span below is the earliest GPU start to the latest GPU end in that
buffer, excluding the first 30 buffers of each case. Overlapping vertex and
fragment intervals must not be added as if they were serial work.

| Case | GPU buffer span median (min–max), ms |
|---|---:|
| Off, no wall | 3.480 (2.568–6.198) |
| On, no wall | 11.495 (4.262–12.470) |
| Off, wall | 3.932 (2.620–7.683) |
| On, wall | 6.223 (6.170–6.870) |

Thus an independent GPU execution track shows the same inflation; it is not
merely the conversion of Vulkan ticks to nanoseconds. This is a separate run,
not a claim of exact agreement with a particular printed pass sample.
The `gpu-performance-state-intervals` export records Minimum, Medium and Maximum
states in the first case, predominantly Minimum in the second and third, and
only Minimum in the fourth. GPU buffers arrive roughly 28–29 ms apart, leaving
substantial gaps. The trace does not provide exact clock frequencies or isolate
a fixed-frequency cost for each pass.

Representative steady frames contain 36 counter-sample blit records without
occlusion and 42 with it, plus render and compute records. This establishes use
of Metal counter sampling rather than the CPU timestamp fallback.
[MoltenVK v1.4.1's sampling implementation](https://github.com/KhronosGroup/MoltenVK/blob/v1.4.1/MoltenVK/MoltenVK/Commands/MVKCommandBuffer.mm)
defers stage samples to encoder boundaries, using small blit encoders and fences.
It does not measure each Vulkan command independently. Its
[timestamp-period implementation](https://github.com/KhronosGroup/MoltenVK/blob/v1.4.1/MoltenVK/MoltenVK/GPUObjects/MVKDevice.mm)
keeps Apple GPU timestamp period at 1 ns; its dynamic period filter is for
non-Apple GPUs. Changing that conversion would not address this finding.

## Reproduction and reporting

Use the Apple Clang local presets. The local Release preset defaults to
RelWithDebInfo, so explicitly configure `-DCMAKE_BUILD_TYPE=Release`:

```sh
cmake --preset macos-debug-local
cmake --preset macos-release-local -DCMAKE_BUILD_TYPE=Release
cmake --build --preset macos-debug-local --target renderer_tests
cmake --build --preset macos-release-local --target renderer_tests
export SDL_VULKAN_LIBRARY="$HOME/VulkanSDK/1.4.341.1/macOS/lib/libvulkan.1.dylib"
export VK_DRIVER_FILES="$HOME/VulkanSDK/1.4.341.1/macOS/share/vulkan/icd.d/MoltenVK_icd.json"
export VK_LAYER_PATH="$HOME/VulkanSDK/1.4.341.1/macOS/share/vulkan/explicit_layer.d"
export SONNET_BENCH_AA=none
./build/macos-debug-local/modules/renderer/renderer_tests 'ten thousand draws and a hundred lights at 1080p'
./build/macos-release-local/modules/renderer/renderer_tests 'ten thousand draws and a hundred lights at 1080p'
xcrun xctrace record --template 'Metal System Trace' --output /tmp/occlusion.trace \
  --launch -- ./build/macos-debug-local/modules/renderer/renderer_tests \
  'ten thousand draws and a hundred lights at 1080p'
xcrun xctrace export --input /tmp/occlusion.trace --toc
```

For an independent pacing check, apply the temporary sleep described above to
Release, run it, remove it, rebuild and repeat. Restore the Debug shader directory
before rebuilding after a shader swap. Do not run GPU benchmarks concurrently.

The benchmark now calls its GPU total a **sampled GPU pass sum**, prints its
available/total pass count, and labels missing pass queries **unavailable**.
The final readback pass is normally absent from the earlier slot being measured;
its old printed 0.000 ms was not a measured zero. CPU pass timings still describe
the current recording. The RHI timestamps and renderer scheduling are unchanged:
discarding real elapsed measurements or rescaling them to match Release would
hide the power-state effect rather than correct it. Missing-query handling is a
separate issue from elapsed execution time; see the
[iOS timestamp investigation](iphone-m14-timestamps.md).

## Validation of the reporting change

Both Apple Clang Debug and Release renderer test targets build. The full Debug
renderer suite passes 40,781 assertions in 127 test cases on the real GPU with
validation enabled. The final Release benchmark passes all 16 assertions; all
four cases print the readback as unavailable, with 18/19 available passes without
occlusion and 21/22 with it. Documentation, module dependencies, version
consistency and clang-format 20 checks pass. Linux and Windows execution is left
to CI; this investigation does not establish timing behavior on other drivers.
