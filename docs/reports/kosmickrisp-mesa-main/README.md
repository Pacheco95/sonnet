# KosmicKrisp timestamp validation on Mesa main

[Issue 377](https://github.com/Pacheco95/sonnet/issues/377), run on 2026-10-22.
Apple M4 Max, macOS 26.7.1 (25G241), Xcode 26.3 (17C529), macOS SDK 26.2.
The bug still reproduces on unpatched Mesa main. No engine changes or upstream posts.

| Build / revision | Program | Result |
|---|---|---|
| Mesa main `73244ce974816fb41ccfb2fb3440cb8049b0dbed`, commit 2026-10-22T22:32:18Z | KosmicKrisp-only debug build | PASS |
| Same main build | Vulkan COUNT=16, five runs | FAIL in all five: queries 4–15 zero and available |
| Same main build | Vulkan COUNT=5, one run | FAIL: query 4 zero and available |
| Same main build | Vulkan COUNT=1, 2, 3, 4, one run each | PASS: all nonzero and available |
| Stock SDK 1.4.363.0, `269a955c898df283fc36f4827643769068cf3d29` | Prior issue 376 Vulkan runs | Same pattern; smallest failing COUNT=5 |

Smallest failing COUNT is 5; counts 1–4 pass. At COUNT=16, queries 0–3
share one nonzero tick, then queries 4–15 return zero, all with availability 1.
Each failing probe exits 1; passing probes exit 0. There is no observed difference
from the stock SDK timestamp pattern. No cause is inferred. COUNT 34 and 64
were compiled but not run because COUNT 16 failed.

## Dependencies and build

Used Step 1 route 2, a source-built translator against the installed LLVM 22.1.0.
`brew search spirv-llvm-translator` found only the unversioned formula (23.1.2).
`llvm@22` exists at 22.1.8 but was not installed: the existing LLVM was retained.
The requested `llvm_release_221` branch does not exist; the nearest available
`llvm_release_220` declares version 22.1.0.0 and works with LLVM 22.1.0.
Translator revision: `67b3fc338a3ce0ebce5d249cbc437e8b36ffdeaa`.
CMake fetched its pinned SPIRV-Headers; SPIRV-Tools 2026.4.1 came from the existing
SDK. Mesa did not request libclc for this configuration, so none was built.
No Homebrew packages or global settings were changed. Dependency setup completed
well within the issue's 90-minute limit.

```sh
git clone --depth 1 --branch llvm_release_220 \
  https://github.com/KhronosGroup/SPIRV-LLVM-Translator.git /tmp/sonnet-377/translator
export PKG_CONFIG_PATH=/tmp/sonnet-368/sdk/macOS/lib/pkgconfig
cmake -S /tmp/sonnet-377/translator -B /tmp/sonnet-377/translator-build -G Ninja \
  -DLLVM_DIR=/opt/homebrew/opt/llvm/lib/cmake/llvm \
  -DCMAKE_INSTALL_PREFIX=/tmp/sonnet-377/prefix -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_SPIRV_INCLUDE_TESTS=OFF -DBUILD_SHARED_LIBS=OFF
ninja -C /tmp/sonnet-377/translator-build -j 8 install

git clone --depth 1 https://gitlab.freedesktop.org/mesa/mesa.git /tmp/sonnet-377/mesa
export PATH=/tmp/sonnet-376/venv/bin:/opt/homebrew/opt/llvm/bin:/opt/homebrew/bin:/usr/bin:/bin
export PKG_CONFIG_PATH=/tmp/sonnet-377/prefix/lib/pkgconfig:/tmp/sonnet-368/sdk/macOS/lib/pkgconfig
export CMAKE_PREFIX_PATH=/tmp/sonnet-377/prefix:/tmp/sonnet-368/sdk/macOS
meson setup /tmp/sonnet-377/mesa-build /tmp/sonnet-377/mesa \
  --buildtype=debug -Dplatforms=macos -Dvulkan-drivers=kosmickrisp \
  -Dgallium-drivers= -Dopengl=false -Dzstd=disabled --prefer-static
ninja -C /tmp/sonnet-377/mesa-build -j 8
```

The first translator build was shared. Mesa configured successfully, then its
`mesa_clc` build tool failed to load the translator dylib. A process-local
`DYLD_LIBRARY_PATH` retry did not help; setting `CMAKE_INSTALL_NAME_DIR` also
left the library's install name unchanged. Reconfiguring the translator with
`BUILD_SHARED_LIBS=OFF`, installing it, and running Mesa setup with
`--clearcache --reconfigure` selected the static library. The final build passed.
The retained logs include each attempt with exact commands and exit codes;
[mesa-build-final.txt](mesa-build-final.txt) is the successful final build.
Both source trees remained clean, as recorded in their source-status logs.

## Driver selection and reproduction

The SDK loader reports Vulkan Instance Version 1.4.363. The corrected summary
[main-summary-corrected.txt](main-summary-corrected.txt) identifies KosmicKrisp,
`driverVersion=26.99.99`, `driverInfo=Mesa 27.0.0-devel (git-73244ce974)`.
`main-info.txt` records `timestampPeriod=41.6667`; probes print
`41.666668`.

Initially, putting only the SDK lib directory in `DYLD_LIBRARY_PATH` silently
substituted its same-named driver, with a loader warning. The
`loader-sdk-substitution-*` files retain that attempt and are **not main results**.
Putting the main driver's directory first fixed selection; the summary then
reported the correct main revision and no path-substitution warning.

```sh
export DYLD_LIBRARY_PATH=/tmp/sonnet-377/mesa-build/src/kosmickrisp/vulkan:/tmp/sonnet-368/sdk/macOS/lib
export VK_DRIVER_FILES=/tmp/sonnet-377/mesa-build/src/kosmickrisp/vulkan/kosmickrisp_mesa_devenv_icd.aarch64.json
/tmp/sonnet-368/sdk/macOS/bin/vulkaninfo --summary
/tmp/sonnet-368/sdk/macOS/bin/vulkaninfo
cc /tmp/sonnet-377/vk-timestamps-count16.c -o /tmp/sonnet-377/vk-timestamps-count16 \
  -I/tmp/sonnet-368/sdk/macOS/include -L/tmp/sonnet-368/sdk/macOS/lib -lvulkan
/tmp/sonnet-377/vk-timestamps-count16
```

The source is [issue 376's exact reproducer](../kosmickrisp-upstream-repro/vk-timestamps.c).
Variants change only `#define COUNT 16` to the tested count. The five
`vk-main-count16-run*.txt` files, `vk-main-count5-run1.txt`, and counts 1–4 retain
every printed tick and availability without filtering or clamping. Each log
starts with its exact invocation and ends with its exit code. All Xcode, meson,
ninja, Vulkan and GPU commands ran outside the sandbox. Logs were redacted in
memory before writing: personal names, paths and hardware UUIDs are removed;
software revisions, versions and raw timestamp values are preserved.

Trailing whitespace in captured logs is normalized; values and diagnostics are intact.

The bulky logs (`formula-versions.txt`, `main-info.txt`, `mesa-build.txt`, `mesa-configure-static.txt`, `mesa-configure.txt`, `mesa-meson-log-final.txt`, `mesa-meson-log.txt`) are archived in [build-logs.tar.gz](build-logs.tar.gz); `tar tzf build-logs.tar.gz` lists them.
