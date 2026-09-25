---
name: setup-sonnet
description: Set up a Linux machine to build and test Sonnet - check the prerequisites, guide the installs, bootstrap vcpkg, configure, build, run the tests on Lavapipe, take an editor screenshot, and on request write a VS Code configuration that builds, runs and debugs the editor and the tests. Use when someone asks to set up, bootstrap or first build the project on Linux, or when a fresh build fails for a missing prerequisite - a CMake configure or vcpkg install error, missing X11 or Wayland headers, no C++23 compiler, no Vulkan 1.4 device, or a sanitizer or coverage preset that will not configure. Also use when someone wants VS Code set up to build, run or debug Sonnet on Linux.
argument-hint: "[check | vscode | <linux preset>]"
allowed-tools: Bash(python3 tools/check_setup.py), Bash(ldconfig -p), Bash(vulkaninfo --summary), Bash(git -C * status --porcelain), Bash(python3 .claude/skills/setup-sonnet/scripts/vscode.py *)
---

# Set up Sonnet on Linux

Take this machine from a fresh clone to a green test run and an editor screenshot, and, if the user wants it, a VS Code configuration. Linux only. For Windows or macOS, point to `docs/build.md` and stop.

Arguments: `$ARGUMENTS`
- empty: the whole setup with the `linux-debug` preset.
- `check`: step 1 only, the report and nothing else.
- `vscode`: step 5 only, for a preset that is already configured: `linux-debug`, or the one the user names. Run step 1 first for the Lavapipe manifest, without the question at its end.
- a preset name (`linux-release`, `linux-asan`, `linux-tsan`, `linux-coverage`): the whole setup with that preset.

`tools/check_setup.py` does the checking. It is read-only, takes under a second, and its output is the source of truth for what is missing. Don't re-check by hand what it already reports. Read `docs/build.md` (Toolchains, Presets, Building and testing) before step 3.

Running the skill again on a machine that is already set up is expected. Every step checks before it changes anything, so a second run only configures, builds and tests.

## Rules

- **Never run `sudo` yourself.** It needs a password, and installing system packages is the user's decision. Show the exact command and ask the user to run it, either in the session as `! sudo ...` or in a terminal of their own if the session cannot prompt for the password. Then check again.
- **Ask before writing outside the repository**: cloning or updating vcpkg, or editing the shell's startup file. Say where and why.
- **Name third-party package sources before suggesting them.** apt.llvm.org (Clang 20, clang-format 20) and `ppa:kisak/kisak-mesa` (Mesa for Lavapipe 1.4) are not the distribution's. CI uses both.
- Configure and build run with `run_in_background` and a log in the scratchpad. You are called back when they finish. Report progress when there is some, don't poll.
- A failure is reported as it happened, with the relevant log lines. Don't retry a build or configure command unchanged, and don't edit engine code or tests to get a green run. A bug found here is reported for the user to decide on.

## 1. Check

```bash
python3 tools/check_setup.py
```

Summarise it in a few lines: what is missing, what is only a warning, and the compiler the report's `configure with:` lines give for the chosen preset. With `check` as the argument, stop here.

Then ask whether to write a VS Code configuration for building, running and debugging the editor and the tests (step 5). Ask now, while the user is here: the configure and build that follow can take an hour. Say what it is: four files in `.vscode/`, which git ignores, for this preset, with gdb through Microsoft's C/C++ extension. If `.vscode/` already exists, say so, and that nothing in it is replaced without their say. The answer only decides step 5, so carry on with step 2 either way. Ask once: not again when step 1 runs a second time.

## 2. Fix what is missing

Work through the `MISSING` lines, in the report's order:

- **System packages.** The report ends with one `sudo apt-get install -y ...` line covering every missing package. Hand it to the user as described in Rules. On a distribution without apt, translate the package names and say that you did.
- **No C++23 compiler.** Ubuntu 24.04's defaults (GCC 13, Clang 18) are too old. `gcc-14 g++-14` from the distribution is the simplest fix.
- **No vcpkg.** Ask, then run the report's `fix:` line. It clones into `$VCPKG_ROOT` when that is set and into `~/vcpkg` otherwise, so the path matches what the presets read.
- **vcpkg lacks the manifest's baseline.** First run `git -C <vcpkg root> status --porcelain`. If it shows local changes, stop and show them: pulling over someone's edits is their call. Otherwise ask, then run the report's `fix:` line, which pulls and bootstraps again so the tool matches the ports.
- **Lavapipe below Vulkan 1.4.** A newer Mesa is needed. First `sudo apt-get update && sudo apt-get install --only-upgrade mesa-vulkan-drivers`: Ubuntu 24.04.5's own Mesa already gives Lavapipe 1.4.318. Only if that is not enough, `ppa:kisak/kisak-mesa`, which CI adds (third party, see Rules).

Then run step 1 again. If something required is still missing because the user declined or could not install it, don't loop: stop and go to step 6 with what is missing and why.

Warnings are optional. Mention them without pushing, except where one blocks the chosen preset:

- **No Clang with the sanitizer runtime** blocks `linux-asan` and `linux-tsan`. Clang 20 comes from apt.llvm.org (third party): `curl -fsSL https://apt.llvm.org/llvm.sh | sudo bash -s -- 20`. The script also needs `lsb-release`, `wget`, `gnupg` and `software-properties-common`.
- **No gcovr** blocks `linux-coverage`, whose configure stops without it: `pip install --user gcovr`, or `pipx install gcovr`.
- **Less memory than one job per core needs:** use the report's `CMAKE_BUILD_PARALLEL_LEVEL` and `VCPKG_MAX_CONCURRENCY` on both the configure and the build in step 3.
- **Little free disk:** the first configure needs about 20 GB. Say so before starting it.
- **`VCPKG_ROOT` not set:** set it inline on every `cmake` call (`VCPKG_ROOT=$HOME/vcpkg cmake --preset ...`). Or offer to export it from the startup file that non-interactive shells read too: `~/.zshenv` for zsh, not `~/.zshrc`. First grep that file and the interactive one for `VCPKG_ROOT`. If the line is already in `~/.zshrc`, suggest moving it rather than adding a second one.
- **No commit-msg hook:** run `sh tools/install_hooks.sh`. It only writes into `.git/hooks`.
- **clang-format not version 20:** CI checks formatting with clang-format 20, and other versions may disagree. It comes from apt.llvm.org too.
- **No validation layer:** the engine runs and says in its log that validation is off. The LunarG SDK provides a 1.4 layer.

## 3. Configure, build, test

Take the compiler from the report's `configure with:` line for the chosen preset. `linux-asan` and `linux-tsan` get a Clang with the sanitizer runtime, as in CI. If the report lists `build/<preset>` under "Existing build directories", keep the compiler recorded there unless the user wants to change it. A different compiler makes CMake drop its cache and vcpkg rebuild every port. If that line says `configure with --fresh`, add `--fresh` to the configure.

```bash
CC=<cc> CXX=<cxx> VCPKG_ROOT=<root> cmake --preset <preset>
cmake --build --preset <preset>
VK_DRIVER_FILES=<Lavapipe manifest from the report> ctest --preset <preset> -j$(nproc)
```

Leave `VCPKG_ROOT=` out when the report found it set.

- **Configure.** The first configure builds every vcpkg port and can take a long time. Warn the user, then run it in the background with a log. vcpkg keeps built ports in its binary cache (`~/.cache/vcpkg/archives`), so a second build directory is quicker.
  - A port that fails to build: read `build/<preset>/vcpkg-manifest-install.log` and the port's logs under `<vcpkg root>/buildtrees/<port>/`. A missing system header or tool is usually named there. Map it to a package and go back to step 2.
- **Build**, in the background too. The first build compiles the whole engine.
- **Test.** The tests run on Lavapipe with `VK_DRIVER_FILES`, as CI does, and in parallel, which is safe: 23 s serially, 11 s with `-j16` on a 16-core machine. Every test has a 300 s timeout. Report the counts and name every failure with its output.
  - A test that fails once can be flaky, like the hang in #24. Run that one test once more on its own (`ctest --preset <preset> -R <name>`) and report both results. Two failures are a failure, and a pass the second time is a flaky test, reported as such. Neither is fixed here.
  - The `linux-tsan` test preset sets its own `VK_DRIVER_FILES` to `/dev/null` so the GPU cases skip. Don't set it there.
  - With `linux-coverage`, the report is the point. After the tests, run `VK_DRIVER_FILES=<manifest> cmake --build --preset linux-coverage-report`, which runs the tests again and then gcovr. Report its `lines`, `functions` and `branches` lines and the path of `build/linux-coverage/coverage/index.html`.

## 4. See the editor draw

Skip this step if the report said there is no display, and say so.

```bash
./build/<preset>/apps/editor/sonnet_editor apps/samples/basic --screenshot <scratchpad>/view.png
```

It exits 0 once the file exists. Read the PNG and describe what it shows: the basic sample's start scene, a ground with a spinning box, a sphere, a cylinder, a capsule and crates under a sky. Never take a screen capture instead (CLAUDE.md, "Seeing what the engine draws").

- **Exits 1 with `Installed Vulkan doesn't implement either the VK_KHR_xcb_surface extension or the VK_KHR_xlib_surface extension`.** This is [#27](https://github.com/Pacheco95/sonnet/issues/27): SDL found vcpkg's loader in the build directory instead of the system's. Run it again with the system's library directory first, `LD_LIBRARY_PATH=/lib/x86_64-linux-gnu` (the directory of the `libvulkan.so.1` that `ldconfig -p` lists), and tell the user the fix is planned in `docs/roadmap.md` under "Before M9". The fix for #27 removes this bullet.
- **Exits 1 because no GPU meets Vulkan 1.4.** Run it on Lavapipe with `VK_DRIVER_FILES`, and say that it is slow there.

Sanitizer presets instrument the editor but not the drivers it loads, so there the exit code alone doesn't say whether the screenshot worked. Judge by whether the PNG exists and by the frames in the sanitizer's report:

- **`linux-tsan`:** don't try the GPU. The NVIDIA driver's own threads crash inside TSan's allocator (`ThreadSanitizer: SEGV` right after the Vulkan instance is created, no PNG). Take the screenshot on Lavapipe with `VK_DRIVER_FILES`. It takes about 40 s, writes the PNG, then exits 66 on races inside Mesa, LLVM and the validation layer. These are the same uninstrumented threads the test preset keeps out with `VK_DRIVER_FILES=/dev/null`.
- **`linux-asan`:** on the NVIDIA GPU it writes the PNG, then exits 1 because LeakSanitizer reports about 2 KB leaked by the driver through `libdbus`.

In both cases, count the screenshot as taken, and report the exit code and whose frames the reports carry. Judge a report by the top frames of each access, lock or allocation it names, not by the whole stack. Engine code (`modules/`, `apps/`) always shows up further down, as the caller into Vulkan, and that alone does not make a report the engine's. A report whose top frame is engine code is a finding: name it for the user, and don't suppress it.

## 5. VS Code

Only if the user said yes in step 1, or passed `vscode`. It needs the preset configured, so skip it if step 3's configure did not succeed, and say so. A failed test or screenshot does not stop it: the configuration is for debugging exactly those.

```bash
python3 .claude/skills/setup-sonnet/scripts/vscode.py --preset <preset> --icd <Lavapipe manifest from the report> [--env KEY=VALUE ...] [--editor-env KEY=VALUE ...]
```

The script takes the compiler from `build/<preset>/CMakeCache.txt` and `VCPKG_ROOT` from the environment (or `--vcpkg-root`), and writes:

- `tasks.json`: `configure`, `build` (the default build task), `test` (all tests on Lavapipe, in parallel, the default test task) and `test one module`.
- `launch.json`: `editor: basic sample` and `tests: one module`, under gdb, each building first. The test launch asks for the module and a Catch2 test spec, and runs on Lavapipe with the test preset's environment, so a failure seen in `ctest` reproduces under the debugger.
- `settings.json`: IntelliSense and clangd read the preset's `compile_commands.json`. CMake Tools, if installed, gets the same compiler and `VCPKG_ROOT` and does not configure on open. From a desktop launcher VS Code has neither, and a configure with a different compiler rebuilds every vcpkg port.
- `extensions.json`: recommends `ms-vscode.cpptools`, which provides the `cppdbg` debugger.

VS Code started from the desktop does not inherit what the shell sets. Pass what this run needed, since the debugger would otherwise run with a different environment than the one that worked here:

- `--env` for each variable the Vulkan SDK's `setup-env.sh` set in this session (`VK_ADD_LAYER_PATH`, and `LD_LIBRARY_PATH` when it points into the SDK), so validation stays on under the debugger. Leave them out when the session has none.
- `--editor-env LD_LIBRARY_PATH=...` when step 4 needed the #27 workaround, and `--editor-env VK_DRIVER_FILES=<manifest>` when the editor only ran on Lavapipe. `--editor-env` takes precedence over `--env` for the editor.

The script leaves alone any file that already exists and differs, prints its diff and exits 1. Show the user the diff and ask. Rerun with `--force` only if they want the files replaced, since `--force` loses whatever they changed by hand. A file that matches is reported as `same`.

Don't try to open VS Code or install the extension. Tell the user to open the repository folder and accept the recommended extension. Then F5 picks a launch configuration, Ctrl+Shift+B builds, and `Tasks: Run Test Task` runs the tests. The configuration is for one preset. After configuring another preset, run the script again with that preset and `--force`.

## 6. Report

End with a short summary: what was installed or changed on the machine and by whom, the preset and compiler, the test counts (and any flaky test), whether the screenshot was taken, whether the VS Code configuration was written (and any file kept), and what is still missing or left as a warning. Then give the three commands to keep, configure, build and test, with the actual compiler, vcpkg root and Lavapipe manifest filled in, exactly as they worked here.
