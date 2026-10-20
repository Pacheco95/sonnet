# Apple GPU performance state and occlusion timings

Follow-up to [issue 354](https://github.com/Pacheco95/sonnet/issues/354) and the
[Debug occlusion investigation](moltenvk-debug-occlusion-timings.md), 2026-10-20,
starting from `ee953fe`.

## Finding

A recording-scoped Maximum state request removes the disproportionate Debug
occlusion timing inflation in this workload. Independent Metal execution spans
for Debug with occlusion fall from 11.473 to 2.704 ms without a wall and from
6.225 to 1.492 ms with a wall: about 76% shorter elapsed execution in both cases.
Release at the same requested and observed state measures 2.733 and 1.513 ms.
Across all four cases, the Debug and paced Release pooled Metal medians are
within 1.4% of the corresponding Release medians at Maximum. This quantifies the
performance-state dependence; it does not establish fixed GPU MHz.

The device-state track covers 100% of the union of timed Metal execution
intervals in every controlled recording, with observed and desired states both
Maximum. Default-policy Debug with occlusion is 99.71–100% Minimum without a
wall and 100% Minimum with a wall. Thus this is more than a requested-state label
or an inference from timing alone. CPU submission gaps remain large under
Maximum; the GPU executes faster despite those gaps. Debug wall clock remains
about 29 ms per frame. The paced Release control converges too, without removing
its 26 ms sleeps.

There is no remaining disproportionate Debug penalty requiring an upstream defect
report from these measurements. Small differences and a wider paced-Release tail
remain: at Maximum with a wall and occlusion, its p90 is 1.799 ms versus Release's
1.549 ms. The categorical state track cannot isolate exact frequency, memory,
scheduling or sampling contributions to that tail. A claim about such a residual
would need a smaller reproduction and repeatable evidence at matched conditions.

Sustained untraced Release remains the performance reference. Its median wall
clock is 2.612 / 2.744 ms with occlusion off/on without a wall, and 1.712 / 1.515 ms
with a wall: +5.1% and -11.5% by the ratios of those medians. Controlled profiling
explains the misleading Debug comparison; it does not replace that reference.

## Scope and method

Apple M4 Max, macOS 26.7.1 (25G241), Xcode 26.3 (17C529), Apple Clang 17,
SDK MoltenVK 1.4.1. This is a live, offscreen Vulkan benchmark translated through
MoltenVK, not a replay of a captured Metal frame. The earlier report records Xcode
26 without a minor version; its numbers are historical context, not the repeated
default-policy controls here.

The existing ten-thousand-draw benchmark runs AA None, four cases in order:
occlusion off without a wall, on without a wall, off with a wall, on with a wall.
Each case has 30 warmup and 120 timed frames, including the final wait/readback.
Debug and explicit `CMAKE_BUILD_TYPE=Release` use their own unmodified shaders,
with the existing validation settings. A separate Release binary inserts a
26 ms sleep before every `beginFrame` in `GpuScene::render`; that temporary
source change is removed and ordinary Release rebuilt before collecting data.
No engine scheduling, timestamp conversion or runtime power policy is changed.

Three repetitions run sequentially on the same device. Each repetition runs
untraced, default Metal System Trace, then the same template with Maximum
requested. Within those policies, build order is Debug / Release / paced Release
in repetition 1, paced Release / Release / Debug in repetition 2, and
Release / Debug / paced Release in repetition 3. Case order stays fixed.
The initial Maximum Debug probe is excluded from these repetitions.

The clean collection runs from 17:19:07 to 17:33:35 UTC on 2026-10-20.
The Mac is on AC power, battery 80%, not charging, `powermode 0` on AC and battery.
`pmset -g therm` is saved before each run; all 27 snapshots report no recorded
thermal warning, performance warning or CPU power status. No other GPU benchmark
runs in parallel. The default job pool starts 13 workers.
These observations do not measure die temperature, fan speed or exact GPU MHz;
normal desktop/compositor activity remains present. There is no deliberate
cooldown or thermal preconditioning. Compare the traced default and Maximum
pairs as profiler experiments, and untraced sustained Release as the performance
reference.

## Verifying the live control

Apple's [shader inspection guidance](https://developer.apple.com/documentation/xcode/inspecting-shaders)
describes choosing a consistent performance state in Xcode's GPU Profiler while
iterating on captured shaders. That alone does not prove a live benchmark runs at
that state. On this Xcode installation, Metal System Trace also contains a
`gpuperformancestate` recording option. A temporary copy of the installed template
requests value 3 (Maximum), leaving the installed template untouched. The
installed GPU instrument model maps values 1, 2 and 3 to Minimum, Medium and
Maximum. This template encoding is an observed Xcode 26.3 implementation detail,
not a stable command-line API. Recheck it when changing Xcode.

For each recording, export both `gpu-performance-device-state-intervals` and
`gpu-performance-state-intervals`. The former separates observed `state` from
`desired-state`; the latter labels induced intervals. A TOC attribute such as
`requested-consistent-state="3"` proves only the request. The initial
`gpu-performance-state-info` row can still say enabled/sustained No at timestamp
zero; do not use that startup row to classify the later workload. Verify actual
intervals overlapping the timed execution instead.

The state track is categorical and has gaps outside reported activity. A Maximum
observation does not certify a fixed frequency, nor eliminate every thermal or
memory-system contribution. Missing state coverage must be reported separately.
Apple's [live counter discussion](https://developer.apple.com/videos/play/tech-talks/10001/)
explains clock-dependent activity, power balancing and stage-boundary sampling on
Apple Silicon. Its vendor timestamp conversion discussion is for Intel/AMD.
[MoltenVK's guide](https://github.com/KhronosGroup/MoltenVK/blob/v1.4.1/Docs/MoltenVK_Runtime_UserGuide.md)
specifies Apple Silicon results in nanoseconds independent of power-state
changes; its [period filter](https://github.com/KhronosGroup/MoltenVK/blob/v1.4.1/Docs/MoltenVK_Configuration_Parameters.md#mvk_config_timestamp_period_lowpass_alpha)
is ignored on Apple Silicon. None of this justifies rescaling the timestamps.

## Measurement interpretation

Wall clock is elapsed time divided by the 120 timed frames. The Vulkan pass sum
is one earlier frame's available queries per run, not a frame-time distribution.
Keep unavailable queries unavailable: normally the readback query is missing,
with 18/19 available passes without occlusion and 21/22 with it. A run with no
available queries has an unavailable sum, even if the benchmark prints 0.000;
exclude that missing observation from numerical summaries and retain its count.

For independent Metal measurements, filter `metal-gpu-intervals` to the benchmark
process, group by command-buffer id, and sort by earliest GPU start. There must
be 600 benchmark buffers: four groups of 150. Drop the first 30 of each group.
A buffer span is latest stage end minus earliest stage start. Never add overlapping
vertex, fragment, compute or blit stage durations as serial work. The span includes
internal waits and is not identical to the partial Vulkan pass sum. Pool the 120
steady buffers from each of three runs for the Metal distribution; those 360
samples are clustered within three runs, not 360 independent experiments.

Inter-buffer GPU idle gaps are next buffer start minus preceding buffer end,
clamped at zero. Submission spacing uses consecutive end times of
`metal-application-command-buffer-submissions` (creation plus duration). Neither
metric is a pass execution time. Intersect the union of timed Metal execution
intervals with the device-state intervals to report observed state and coverage;
exclude warmup and inter-buffer idle time from that denominator.

## Results

The initial collection was stopped after the operator reported concurrent
YouTube playback. Its measurements are preliminary and excluded from the final
comparison: video decoding, compositing and shared power or memory activity can
confound both policy and pacing controls. The complete matrix was restarted
after playback stopped; none of the earlier observations are combined with it.

The tables below use only the restarted clean collection. `plain` means
untraced; `default` means the built-in Metal System Trace policy; `maximum`
means the copied template's recording-scoped request. `paced` is Release with
the diagnostic sleep. All values derive from the checked-in
[per-run results](apple-gpu-performance-state-results.csv) and
[timed Metal samples](apple-gpu-performance-state-samples.csv).

The results CSV has one row per run and case, including UTC run start, recorder
process duration, wall clock, pass availability, Metal span/gap summaries,
submission spacing and state percentages. A blank sampled sum means unavailable,
not zero. The samples CSV has one row per traced run and case and 120 ordered
buffer spans in milliseconds (`sample_000_ms` through `sample_119_ms`), after
warmup exclusion. Those 72 rows reproduce the pooled distributions. Complete
recording logs, traces and exports are retained locally in
`/tmp/sonnet-354-clean`; the interrupted batch is separate in `/tmp/sonnet-354`.

### Wall clock

Milliseconds: median (minimum–maximum) across three runs.

| Policy / build | Off, no wall | On, no wall | Off, wall | On, wall |
|---|---:|---:|---:|---:|
| plain / debug | 28.800 (28.542–29.353) | 29.395 (28.967–29.459) | 29.126 (28.579–29.127) | 28.723 (28.661–29.365) |
| plain / release | 2.612 (2.608–2.617) | 2.744 (2.744–2.754) | 1.712 (1.710–1.762) | 1.515 (1.511–1.517) |
| plain / paced | 34.804 (34.524–34.994) | 34.899 (34.869–35.825) | 35.105 (34.478–35.375) | 35.097 (34.856–36.202) |
| default / debug | 29.066 (28.961–29.549) | 29.313 (29.301–30.192) | 28.848 (28.819–29.494) | 29.516 (29.216–29.626) |
| default / release | 2.609 (2.605–2.665) | 2.755 (2.753–2.785) | 1.732 (1.722–1.799) | 1.683 (1.628–1.744) |
| default / paced | 34.907 (34.599–35.000) | 35.243 (35.074–35.557) | 34.696 (33.983–35.336) | 35.488 (35.058–35.591) |
| maximum / debug | 29.315 (28.884–29.425) | 29.351 (29.329–29.432) | 28.857 (28.747–28.917) | 29.240 (29.022–29.273) |
| maximum / release | 2.613 (2.608–2.624) | 2.756 (2.751–2.757) | 1.727 (1.721–1.736) | 1.661 (1.652–1.667) |
| maximum / paced | 34.152 (34.143–34.569) | 35.197 (34.488–35.446) | 34.065 (33.975–34.897) | 34.863 (34.853–35.393) |

### Vulkan sampled pass sum

Milliseconds: median (minimum–maximum) across three runs. The pass-sum table
summarizes three single-frame samples, not all frames. `n` marks missing samples.

| Policy / build | Off, no wall | On, no wall | Off, wall | On, wall |
|---|---:|---:|---:|---:|
| plain / debug | 4.036 (3.960–8.909) | 11.331 (11.257–11.347) | 6.976 (2.634–7.058) | 6.069 (6.054–6.168) |
| plain / release | 2.556 (2.545–2.575) | 2.666 (2.665–2.682) | 1.648 (1.648–1.669) | 1.457 (1.448–1.460) |
| plain / paced | 6.160 (4.953–11.001) | 11.427 (4.501–11.545) | 7.127 (7.095–7.177) | 6.225 (6.094–6.359) |
| default / debug | 3.567 (2.563–4.438) | 11.300 (11.251–11.468) | 3.860 (1.631–4.651) | 6.037 (6.029–6.048) |
| default / release | 2.683 (2.533–2.833) n=2 | 2.678 (2.665–2.690) n=2 | 1.652 (1.640–1.682) | 1.474 (1.466–1.481) n=2 |
| default / paced | 5.705 (4.866–11.237) | 11.434 (11.343–11.574) | 7.104 (7.031–7.321) | 6.110 (6.070–6.176) |
| maximum / debug | 2.551 (2.550–2.556) | 2.648 (2.644–2.651) | 1.655 (1.642–1.726) | 1.454 (1.444–1.455) |
| maximum / release | 2.572 (2.572–2.572) n=1 | 2.717 (2.692–2.725) | 1.666 (1.638–1.674) | 1.468 (1.455–1.526) |
| maximum / paced | 2.549 (2.543–2.579) | 2.665 (2.665–2.674) | 1.704 (1.679–1.712) | 1.459 (1.445–1.477) |

### Independent Metal execution

Buffer span in ms: median [p10–p90], pooling 360 timed buffers per cell.

| Policy / build | Off, no wall | On, no wall | Off, wall | On, wall |
|---|---:|---:|---:|---:|
| default / debug | 3.434 [2.607–4.916] | 11.473 [11.369–11.603] | 2.888 [2.113–7.120] | 6.225 [6.196–6.306] |
| default / release | 2.588 [2.571–2.628] | 2.729 [2.706–2.763] | 1.704 [1.684–1.743] | 1.512 [1.496–1.573] |
| default / paced | 11.208 [4.430–11.551] | 11.574 [4.687–11.894] | 7.237 [7.100–7.472] | 6.348 [6.268–6.542] |
| maximum / debug | 2.591 [2.576–2.632] | 2.704 [2.687–2.746] | 1.694 [1.681–1.716] | 1.492 [1.483–1.528] |
| maximum / release | 2.588 [2.570–2.616] | 2.733 [2.714–2.761] | 1.701 [1.686–1.724] | 1.513 [1.498–1.549] |
| maximum / paced | 2.599 [2.579–2.687] | 2.717 [2.694–2.777] | 1.711 [1.692–1.757] | 1.516 [1.494–1.799] |

### Submission spacing and idle gaps

Milliseconds: median across the three run medians, GPU idle gap / CPU commit
spacing. These describe pacing, separately from GPU execution spans.

| Policy / build | Off, no wall | On, no wall | Off, wall | On, wall |
|---|---:|---:|---:|---:|
| default / debug | 25.416 / 29.076 | 17.654 / 29.115 | 25.619 / 28.773 | 23.220 / 29.432 |
| default / release | 0.001 / 2.583 | 0.001 / 2.725 | 0.001 / 1.715 | 0.125 / 1.647 |
| default / paced | 24.718 / 34.768 | 24.407 / 35.017 | 28.022 / 34.764 | 29.046 / 35.257 |
| maximum / debug | 26.559 / 29.162 | 26.589 / 29.283 | 27.034 / 28.711 | 27.751 / 29.193 |
| maximum / release | 0.001 / 2.586 | 0.001 / 2.735 | 0.001 / 1.711 | 0.112 / 1.637 |
| maximum / paced | 31.471 / 34.031 | 32.347 / 35.033 | 32.518 / 34.232 | 33.245 / 34.957 |

### Observed state coverage

Each cell is observed Maximum percentage / state-track coverage percentage,
using the minimum across the three runs. The denominator is the union of timed
Metal execution intervals; unobserved portions remain unclassified. This checks
the live workload rather than the requested-state label.

| Policy / build | Off, no wall | On, no wall | Off, wall | On, wall |
|---|---:|---:|---:|---:|
| default / debug | 0.00% / 100.00% | 0.00% / 100.00% | 0.00% / 100.00% | 0.00% / 100.00% |
| default / release | 99.21% / 100.00% | 100.00% / 100.00% | 100.00% / 100.00% | 100.00% / 100.00% |
| default / paced | 0.00% / 100.00% | 0.00% / 100.00% | 0.00% / 100.00% | 0.00% / 100.00% |
| maximum / debug | 100.00% / 100.00% | 100.00% / 100.00% | 100.00% / 100.00% | 100.00% / 100.00% |
| maximum / release | 100.00% / 100.00% | 100.00% / 100.00% | 100.00% / 100.00% | 100.00% / 100.00% |
| maximum / paced | 100.00% / 100.00% | 100.00% / 100.00% | 100.00% / 100.00% | 100.00% / 100.00% |

### Tool overhead and limits

Relative to the untraced wall medians, default tracing changes Debug by -0.95%
to +2.76% across cases, and paced Release by -1.17% to +1.11%. Release changes by
-0.11%, +0.40%, +1.17% and +11.09% in case order. In particular, the occluded-wall
Release case rises from 1.515 to 1.683 ms under tracing. These are observed
recording-associated differences, including run variation, rather than an
isolated estimate of every profiler cost. Report the instrumentation alongside
the result; do not treat traced wall time as the sustained performance baseline.
Recording finalization/symbolication is excluded from benchmark wall time. The
results CSV retains the whole launch/record/save process duration separately;
that field is not an isolated finalization timer.

Five Release samples have 0 available queries: Maximum repetition 1 and 2 without
occlusion or wall; default repetition 2 with occlusion and wall; and default
repetition 3 in both no-wall cases. Their sums are unavailable in the data and
excluded from numerical pass-sum summaries. Every other sample has the expected
18/19 or 21/22 queries, with final readback unavailable. Independent Metal
execution remains available for all timed buffers in those recordings.

Metal encoder labels are generic on this path. The independent comparison uses
command-buffer execution bounds, not a claimed one-to-one mapping of Metal
encoders to named Vulkan passes. Vulkan's partial pass sum and the independent
execution distribution agree on the inflation and its removal, but represent
different boundaries and sampling populations. The pass sums must not be
subtracted across default-policy configurations to estimate occlusion cost.

## Validation

All 27 clean runs pass all 16 benchmark assertions each (432 total), including
validation-message and indirect-call checks. All 18 recordings contain exactly
600 benchmark command buffers, with 120 timed buffers in each of four cases after
warmup: 8,640 Metal buffer-span observations in total. They are clustered in
recordings, not independent statistical trials. All controlled
cases have full observed-state coverage at Maximum. The temporary pacing source
edit is restored, ordinary Release rebuilt, and the final change contains only
documentation and measurement data.

## Reproduce on this Xcode version

Build and set the loader environment using the commands in the
[earlier reproduction](moltenvk-debug-occlusion-timings.md#reproduction-and-reporting).
Confirm `CMAKE_BUILD_TYPE:STRING=Release` in the Release cache. Build the temporary
paced binary with the sleep described there, copy it next to `renderer_tests` as
`renderer_tests_paced`, then restore the source and rebuild normal Release.
Both binaries must remain beside the same Release `shaders/` directory.

Create a recording-only Maximum template from the installed template. This
changes the two state-selector references, not shared archived integer objects:

```sh
python3 - <<'PY'
import pathlib
import plistlib
import subprocess

xcode = pathlib.Path(subprocess.check_output(['xcode-select', '-p'], text=True).strip()).parent
source = xcode / ('Applications/Instruments.app/Contents/Packages/GPU.instrdst/'
                  'Contents/Templates/Metal System Trace.tracetemplate')
archive = plistlib.loads(source.read_bytes())
objects = archive['$objects']
changed = []
for item in list(objects):
    if isinstance(item, dict) and 'NS.keys' in item:
        for index, key in enumerate(item['NS.keys']):
            name = objects[key.data]
            if name in ('gpuperformancestate', 'gpuperformancestateinternal'):
                changed.append((name, objects[item['NS.objects'][index].data]))
                item['NS.objects'][index] = plistlib.UID(len(objects))
                objects.append(3)
assert sorted(changed) == [('gpuperformancestate', 0), ('gpuperformancestateinternal', 0)]
pathlib.Path('/tmp/sonnet-maximum.tracetemplate').write_bytes(
    plistlib.dumps(archive, fmt=plistlib.FMT_BINARY))
PY
```

The assert deliberately stops on an unfamiliar template rather than silently
assuming another Xcode version has identical controls. Record one build at a
time; choose either the built-in default template or this copied template:

```sh
export SONNET_BENCH_AA=none
xcrun xctrace record --template /tmp/sonnet-maximum.tracetemplate \
  --output /tmp/occlusion-maximum-debug.trace \
  --target-stdout /tmp/occlusion-maximum-debug.log \
  --env SDL_VULKAN_LIBRARY="$SDL_VULKAN_LIBRARY" \
  --env VK_DRIVER_FILES="$VK_DRIVER_FILES" \
  --env VK_LAYER_PATH="$VK_LAYER_PATH" --env SONNET_BENCH_AA=none \
  --launch -- ./build/macos-debug-local/modules/renderer/renderer_tests \
  'ten thousand draws and a hundred lights at 1080p'
```

Use `--template 'Metal System Trace'` for the default-policy trace and run the
binary directly for the untraced reference. Repeat the same commands with
Release and paced Release in the order above. Record `pmset -g batt`,
`pmset -g custom`, `pmset -g therm`, `sw_vers` and `xcodebuild -version` alongside
run order and start times. Wait for recording finalization before exporting;
the `.trace` directory may exist before it can be opened. Saving/symbolication
time is not benchmark wall time. This installation emits DWARF analysis and
backdated-signpost warnings; preserve the recording logs and verify the expected
600 buffers instead of assuming a successful recorder exit proves completeness.

```sh
for schema in metal-gpu-intervals metal-application-command-buffer-submissions \
  gpu-performance-state-info gpu-performance-state-intervals \
  gpu-performance-device-state-intervals; do
  xcrun xctrace export --input /tmp/occlusion-maximum-debug.trace \
    --xpath "/trace-toc/run[@number='1']/data/table[@schema='$schema']" \
    --output "/tmp/occlusion-maximum-debug-$schema.xml"
done
```

The XML uses `id`/`ref` interning, including process, timestamp and state fields.
Resolve references before filtering or arithmetic. Compute durations from raw
nanosecond values, not localized `fmt` strings. Check the observed state,
requested state and coverage for every case before accepting a comparison.
If this workflow cannot induce or verify the same state on another installation,
report that limitation and keep its timing comparison observational. Do not
substitute High Power Mode, a requested-state label, or shader replay timings as
proof of fixed-frequency live execution.
