# macOS spot shadow check

Spot-shadow sampling passes on MoltenVK at PR #120's merged revision
`6f39c0a0bac495f6a14ebee248b611bedde310a3`. The GPU regression and the basic
sample's on/off viewport captures behave as expected. No source files changed.

## Environment

- macOS 26.7.1 (25G241), Apple M4 Max GPU.
- Apple Clang 17.0.0 (`clang-1700.6.4.2`), arm64; Xcode macOS 26.2 SDK.
- `macos-debug`, using the existing cache's Xcode compiler.
- Vulkan SDK 1.4.341.1; loader 1.4.341; MoltenVK 1.4.1; Vulkan 1.4.334.
- Validation and synchronization validation enabled.

## Commands

Fetch, checkout and fast-forward pull completed on main. The requested clone-only
identity was configured and its email checked; personal identity values are omitted
from this public report. The commit-msg hook already existed.

```sh
git fetch origin && git checkout main && git pull --ff-only
git rev-parse HEAD
git merge-base --is-ancestor 6f39c0a HEAD
export VCPKG_ROOT="$HOME/vcpkg"
cmake --preset macos-debug
cmake --build --preset macos-debug
sw_vers
system_profiler SPDisplaysDataType | rg 'Chipset Model|Metal Support'
/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang++ --version
```

Configuration required a retry outside the sandbox to access vcpkg's lock.
Configuration and the full 310-step build succeeded. Build warnings were Slang
E41012 (profile implicitly upgraded with additional SPIR-V capabilities) and the
linker ignoring duplicate `libSDL3.a` libraries.

Initial tests without SDK settings passed only CPU cases: 42 passed, 20 skipped;
the direct case skipped with no assertions (exit 4). SDL could not load the Vulkan
portability library. Selecting the SDK loaded MoltenVK, but the sandbox prevented
Metal access (`VK_ERROR_INCOMPATIBLE_DRIVER`, Metal unavailable, and loader
`terminator_CreateInstance: Found no drivers!`). The successful runs below used
these settings outside the sandbox:

```sh
export VULKAN_SDK="$HOME/VulkanSDK/1.4.341.1/macOS"
export SDL_VULKAN_LIBRARY="$VULKAN_SDK/lib/libvulkan.1.dylib"
export VK_DRIVER_FILES="$VULKAN_SDK/share/vulkan/icd.d/MoltenVK_icd.json"
export VK_ADD_LAYER_PATH="$VULKAN_SDK/share/vulkan/explicit_layer.d"
build/macos-debug/modules/renderer/renderer_tests "a spot light's shadow darkens*"
ctest --preset macos-debug -R renderer_tests --output-on-failure
mkdir -p docs/reports/mac-spot-shadows
build/macos-debug/apps/editor/sonnet_editor apps/samples/basic --screenshot docs/reports/mac-spot-shadows/basic-spot-on.png --settle-frames 20
cp -r apps/samples/basic /tmp/basic-off
```

The scratch scene was changed with this Python operation (only Spot's SpotLight):

```python
import json
from pathlib import Path
p = Path('/tmp/basic-off/scenes/main.scene.json')
s = p.read_text()
d = json.loads(s)
spot = [e for e in d['entities'] if e.get('name') == 'Spot']
assert len(spot) == 1 and spot[0]['components']['SpotLight']['castsShadows'] is True
start = s.index('"name": "Spot"')
at = s.index('"castsShadows": true', start)
s = s[:at] + s[at:].replace('"castsShadows": true', '"castsShadows": false', 1)
p.write_text(s)
```

```sh
build/macos-debug/apps/editor/sonnet_editor /tmp/basic-off --screenshot docs/reports/mac-spot-shadows/basic-spot-off.png --settle-frames 20
git checkout -b agents/mac-spot-shadows
```

Diagnostic output was retained in temporary logs outside the repository. No raw
machine logs are committed.

## A: tests

Both successful commands exited 0. CTest: **62 cases, 33,979 assertions passed,
none skipped**. Direct spot-shadow regression: **1 case, 14 assertions passed,
none skipped**. This covers the shadow behind the box, lit ground elsewhere,
moving the occluder, disabling castsShadows, zero maxLocalShadows, and two lights
using their own maps. Validation-message-count assertions passed.

CTest tail (path redacted):

```text
Test project ~/repositories/sonnet/build/macos-debug
    Start 5: renderer_tests
1/1 Test #5: renderer_tests ...................   Passed    3.86 sec

100% tests passed, 0 tests failed out of 1

Label Time Summary:
renderer    =   3.86 sec*proc (1 test)

Total Test time (real) =   3.87 sec
```

No MoltenVK warnings or Vulkan validation errors were found in the successful
runs. The debug log includes `WARNING-CreateInstance-status-message`, announcing
that the Khronos validation layer is active with synchronization validation; it
is a startup status message. The full suite logs an expected invalid-texture error
(`texture "wrong": 4 bytes given, 16 expected`), from its rejection test.
Neither capture logged runtime warnings or errors; both exited 0.

## B: viewport observations

Both PNGs were opened and inspected at 968 by 639. They contain only the viewport,
with no title bar, filenames or personal data.

- [Shadows on](mac-spot-shadows/basic-spot-on.png): a pale warm orange/pink cone
  lights the checkerboard ground. Long, soft-edged darker shadows extend away
  from the light behind the cylinder, capsule and blue sphere. They are visibly
  darker than the surrounding lit ground. No apparent displaced or mirrored
  shadows, cone-wide dark/light band, speckle, shadow on the light-facing side,
  or wholly black cone.
- [Shadows off](mac-spot-shadows/basic-spot-off.png): the same warm illumination
  remains, and those long spot shadows disappear. The shorter sun shadows remain.
  The ground formerly covered by the long shadows is lit.

No rendering failure was observed. These are still captures after 20 settle
frames; temporal flicker was not evaluated. The final versus shadow-factor
comparison in check C was not needed and was not run. Shadow-factor diagnoses
the sun only, not spot shadows.

## Report checks

`python3 tools/check_docs.py` passed. The requested personal-data grep was run
before committing: only compressed PNG binary data matched; neither PNG contains
text or EXIF metadata (IHDR, IDAT and IEND chunks only). The viewport PNGs were
also visually inspected for personal content.
The branch contains only this report and the two PNGs; no PR is requested.
