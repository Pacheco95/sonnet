# Standalone KosmicKrisp timestamp validation

[Issue 376](https://github.com/Pacheco95/sonnet/issues/376), run on 2026-10-22.
Apple M4 Max, macOS 26.7.1 (25G241), Xcode 26.3 (17C529).
No engine or installed driver changes, injected libraries, or upstream posts.

| Build tested | Program | Result | Smallest failing COUNT |
|---|---|---|---|
| SDK 1.4.363.0 KosmicKrisp 26.2.99 | Vulkan | FAIL, available zeros; five repeats each at 5 and 16 | 5 (1–4 pass) |
| SDK 1.4.363.0 MoltenVK | Same Vulkan binary | PASS at 16 | Not found; only 16 tested |
| System Metal 4, relaxed | Metal | Four normal ticks, then zeros | 5 among 4, 5, 16 tested |
| System Metal 4, precise | Metal | All samples nonzero and ordered | None among 4, 5, 16 tested |
| Newer LunarG macOS SDK | Vulkan | Unavailable: latest endpoint still lists 1.4.363.0 | Not tested |
| Mesa main `2a1d336b438b957eb948583eefc8ba86ef06cbc9` | Vulkan | Build blocked at configuration: missing LLVMSPIRVLib | Not tested |

## Exact observations

Stock Vulkan at COUNT 16 returns the same nonzero tick for queries 0–3,
then zero for 4–15; all availability values are 1. All five runs exit 1
with `FAIL: available query returned zero or went backwards`. At COUNT 5,
query 4 is zero and available in all five repeats. The pattern is identical
across repeats; absolute nonzero clock values differ. Files retain every
printed tick without clamping or conversion. Counts 1–4 exit 0.
The MoltenVK control at COUNT 16 exits 0, with all queries available,
nonzero and increasing. This control uses the loader and ICD from the same
1.4.363.0 SDK, rather than the engine's static MoltenVK.

Metal relaxed (`precise=0`) has no zeros at count 4, query 4 zero at count 5,
and queries 4–15 zero at count 16. Metal precise (`precise=1`) has no zeros
at any tested count. All six feedback records say `error=none`; exit is 0.
There is no disagreement between these Vulkan and Metal observations.

KosmicKrisp identifies as `vulkan-sdk-1.4.363 (git-269a955c89)`, with
`timestampPeriod=41.6667` (program: `41.666668`). Its complete SDK revision is
`269a955c898df283fc36f4827643769068cf3d29`. The SDK's MoltenVK source revision is
`db66022459ffb663aa2b50f6b018bc2e124f5edf`; its runtime driver information is in
[moltenvk-summary.txt](moltenvk-summary.txt). [sdk-versions.txt](sdk-versions.txt)
retains the SDK's component revisions.

## Newer build attempt

[LunarG's latest macOS endpoint](https://vulkan.lunarg.com/sdk/latest/mac.json)
returned `{"mac":"1.4.363.0"}`; retained in [sdk-latest.json](sdk-latest.json).
Thus no newer SDK was available through that endpoint during this run.

Cloned upstream Mesa main directly from
`https://gitlab.freedesktop.org/mesa/mesa.git`, without patches.
[mesa-revision.txt](mesa-revision.txt) records its revision and commit date.
Used the [official KosmicKrisp build instructions](https://docs.mesa3d.org/drivers/kosmickrisp.html):

```sh
python3 -m venv /tmp/sonnet-376/venv
/tmp/sonnet-376/venv/bin/pip install meson mako packaging pyyaml
export PATH=/tmp/sonnet-376/venv/bin:/opt/homebrew/opt/llvm/bin:$PATH
export PKG_CONFIG_PATH=/tmp/sonnet-368/sdk/macOS/lib/pkgconfig
export CMAKE_PREFIX_PATH=/tmp/sonnet-368/sdk/macOS
meson setup /tmp/sonnet-376/mesa-build /tmp/sonnet-376/mesa \
  --buildtype=debug -Dplatforms=macos -Dvulkan-drivers=kosmickrisp \
  -Dgallium-drivers= -Dopengl=false -Dzstd=disabled --prefer-static
```

Configuration finds LLVM 22.1.0 but fails at `meson.build:2083` because
`LLVMSPIRVLib` is unavailable. Homebrew's available translator is 23.1.2
and libclc is 23.1.4; neither is installed. Mesa requires the translator to
match LLVM's major/minor (22.1 here). Stopped the build attempt within the
one-hour limit rather than replacing the host LLVM installation or patching
Mesa. No main ICD was produced, so its driver info and timestamp behavior
are unknown. This is a build-environment limitation, not a Mesa test failure.
`mesa-configure.txt`,
`mesa-meson-log.txt`, and
[dependency-availability.json](dependency-availability.json) retain the evidence.

## Reproduction and files

All Xcode, Vulkan and GPU commands ran outside the sandbox. Sources and binaries
were built in scratch storage outside the repository. The SDK already existed
at `/tmp/sonnet-368/sdk/macOS`. Explicit loader selection used
`DYLD_LIBRARY_PATH`; no `DYLD_INSERT_LIBRARIES` was set for the probes.

```sh
export VULKAN_SDK=/tmp/sonnet-368/sdk/macOS
export DYLD_LIBRARY_PATH="$VULKAN_SDK/lib"
cc vk-timestamps.c -o vk-timestamps \
  -I"$VULKAN_SDK/include" -L"$VULKAN_SDK/lib" -lvulkan
VK_DRIVER_FILES="$VULKAN_SDK/share/vulkan/icd.d/libkosmickrisp_icd.json" ./vk-timestamps
VK_DRIVER_FILES="$VULKAN_SDK/share/vulkan/icd.d/MoltenVK_icd.json" ./vk-timestamps
xcrun clang -fobjc-arc -mmacosx-version-min=26.0 \
  -framework Foundation -framework Metal metal-probe.m -o metal-probe
./metal-probe
```

[vk-timestamps.c](vk-timestamps.c) and [metal-probe.m](metal-probe.m) are the
issue's exact sources. `vk-timestamps-count1.c` through
`vk-timestamps-count5.c` change only COUNT. No timestamp values were filtered.
`vk-kk-count16-run1.txt` through `run5.txt`, `vk-kk-count5-run1.txt` through
`run5.txt`, and `vk-kk-count1.txt` through `vk-kk-count5.txt` retain every run.
[vk-moltenvk.txt](vk-moltenvk.txt) and [metal-probe.txt](metal-probe.txt) retain
the controls. Empty build logs mean the compiler emitted no diagnostics.
The environment, full Vulkan capabilities, driver summaries, dependency probes,
configuration logs, and fetched documentation are retained alongside them.
Personal identifiers are redacted; clock ticks and software revisions are intact.

The bulky logs (`mesa-configure.txt`, `mesa-kosmickrisp.html`, `mesa-macos.html`, `mesa-meson-log.txt`, `stock-info.txt`) are archived in [build-logs.tar.gz](build-logs.tar.gz); `tar tzf build-logs.tar.gz` lists them.
