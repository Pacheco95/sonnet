#!/usr/bin/env python3
"""Checks a Linux machine for what building and testing Sonnet needs (docs/build.md).

Read-only: it runs version queries and reads files, and installs nothing. Each line is ok, warn or
missing, with the fix after it. Exits 1 when something required is missing, 0 otherwise.
Warnings are for what the build does without, or what only some presets need: validation layers,
a display, clang-format, hooks, a sanitizer runtime, gcovr, and room on disk and in memory.
"""

from __future__ import annotations

import glob
import json
import os
import platform
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# pkg-config module and the Ubuntu package that provides it: the headers the sdl3 overlay port's
# X11 and Wayland back ends are built against (ports/sdl3/portfile.cmake). SDL loads the libraries
# at run time, so only the headers are needed; the port turns the audio, libdecor and KMSDRM back
# ends off, so CI's longer package list is more than a build needs.
DEV_PACKAGES = {
    "x11": "libx11-dev",
    "xext": "libxext-dev",
    "xcursor": "libxcursor-dev",
    "xfixes": "libxfixes-dev",
    "xi": "libxi-dev",
    "xrandr": "libxrandr-dev",
    "xscrnsaver": "libxss-dev",
    "xkbcommon": "libxkbcommon-dev",
    "wayland-client": "libwayland-dev",
    "wayland-egl": "libwayland-dev",
    "wayland-scanner": "libwayland-dev",
    "egl": "libegl-dev",
}

# Tools vcpkg's bootstrap and the ports' builds call, with the Ubuntu package for each.
TOOLS = {
    "git": "git",
    "curl": "curl",
    "zip": "zip",
    "unzip": "unzip",
    "tar": "tar",
    "pkg-config": "pkg-config",
    "ninja": "ninja-build",
    "cmake": "cmake",
}

MIN_CMAKE = (3, 28)
MIN_GCC = 14
MIN_CLANG = 19  # with libstdc++ 14; Clang 18 cannot see std::expected in libstdc++ (docs/build.md)
MIN_LIBSTDCXX = 14
CI_CLANG_FORMAT = 20
MIN_VULKAN = (1, 4)
MIN_FREE_DISK_GB = 20  # the ports' build trees, vcpkg's binary cache and one build directory
MIN_MEMORY_GB_PER_JOB = 2  # the Slang and Jolt ports run out of memory with less per compile job

SANITIZER_PRESETS = ("linux-asan", "linux-tsan")


class Report:
    def __init__(self) -> None:
        self.missing = 0
        self.warnings = 0
        self.apt: list[str] = []

    def ok(self, what: str) -> None:
        print(f"  ok       {what}")

    def warn(self, what: str, fix: str) -> None:
        self.warnings += 1
        print(f"  warn     {what}\n           fix: {fix}")

    def fail(self, what: str, fix: str, apt: list[str] | None = None) -> None:
        self.missing += 1
        print(f"  MISSING  {what}\n           fix: {fix}")
        self.apt.extend(apt or [])


def run(*command: str, env: dict[str, str] | None = None) -> str | None:
    """The command's output, or None when it cannot run or exits non-zero."""
    result = run_status(*command, env=env)
    return result[1] if result and result[0] == 0 else None


def run_status(*command: str, env: dict[str, str] | None = None) -> tuple[int, str] | None:
    try:
        result = subprocess.run(command, capture_output=True, text=True, timeout=30, env=env)
    except (OSError, subprocess.TimeoutExpired):
        return None
    return result.returncode, result.stdout + result.stderr


def version(text: str | None) -> tuple[int, ...]:
    match = re.search(r"(\d+)\.(\d+)(?:\.(\d+))?", text or "")
    return tuple(int(part) for part in match.groups() if part is not None) if match else ()


def dotted(numbers: tuple[int, ...]) -> str:
    return ".".join(str(number) for number in numbers)


def section(title: str) -> None:
    print(f"\n{title}")


def check_system(report: Report) -> bool:
    section("System")
    if platform.system() != "Linux":
        report.fail(f"{platform.system()} host", "this check covers Linux only; see docs/build.md for the others")
        return False
    distro = "unknown distribution"
    if Path("/etc/os-release").exists():
        fields = dict(
            line.split("=", 1) for line in Path("/etc/os-release").read_text().splitlines() if "=" in line
        )
        distro = fields.get("PRETTY_NAME", distro).strip('"')
    report.ok(f"Linux, {distro}, {platform.machine()}")
    if platform.machine() != "x86_64":
        report.warn(f"{platform.machine()} host", "the presets and CI use x64-linux; other triplets are untested")
    if sys.version_info < (3, 10):
        report.fail(f"Python {dotted(sys.version_info[:3])}", "Python 3.10 or later, for the tools/ scripts")
    else:
        report.ok(f"Python {dotted(sys.version_info[:3])}")
    check_resources(report)
    return True


def check_resources(report: Report) -> None:
    free_gb = shutil.disk_usage(ROOT).free / 2**30
    if free_gb < MIN_FREE_DISK_GB:
        report.warn(
            f"{free_gb:.0f} GB free on the repository's disk, about {MIN_FREE_DISK_GB} GB needed",
            "free some space; the first configure builds every vcpkg port",
        )
    else:
        report.ok(f"{free_gb:.0f} GB free on disk")
    try:
        meminfo = Path("/proc/meminfo").read_text()
        memory_gb = int(re.search(r"MemTotal:\s+(\d+)", meminfo).group(1)) / 2**20
    except (OSError, AttributeError):
        return
    cores = os.cpu_count() or 1
    jobs = max(1, int(memory_gb // MIN_MEMORY_GB_PER_JOB))
    if jobs < cores:
        report.warn(
            f"{memory_gb:.0f} GB of memory for {cores} cores; a job per core can run out of memory",
            f"limit the jobs: CMAKE_BUILD_PARALLEL_LEVEL={jobs} VCPKG_MAX_CONCURRENCY={jobs} on configure and build",
        )
    else:
        report.ok(f"{memory_gb:.0f} GB of memory, {cores} cores")


def check_tools(report: Report) -> None:
    section("Build tools")
    for tool, package in TOOLS.items():
        path = shutil.which(tool)
        if path is None:
            report.fail(tool, f"install {package}", [package])
            continue
        if tool == "cmake":
            found = version(run(path, "--version"))
            if found < MIN_CMAKE:
                report.fail(
                    f"cmake {dotted(found)}, 3.28 or later needed",
                    "install a newer CMake (Kitware's apt repository, or `pip install --user cmake`)",
                )
                continue
            report.ok(f"cmake {dotted(found)}")
        else:
            report.ok(tool)


def libstdcxx_major() -> int:
    majors = [int(Path(p).name) for p in glob.glob("/usr/include/c++/*") if Path(p).name.isdigit()]
    return max(majors, default=0)


def has_sanitizer_runtime(clang: str) -> bool:
    """Whether this Clang ships compiler-rt's asan and tsan; a locally built Clang often does not."""
    resource = (run(clang, "-print-resource-dir") or "").strip()
    if not resource:
        return False
    found = {Path(p).name for p in glob.glob(f"{resource}/lib/**/libclang_rt.*.a", recursive=True)}
    return all(any(name.startswith(f"libclang_rt.{kind}") for name in found) for kind in ("asan", "tsan"))


def check_compilers(report: Report) -> None:
    section("Compilers (C++23: GCC 14+, or Clang 19+ with libstdc++ 14+)")
    usable: list[tuple[str, str]] = []

    gcc_names = sorted({Path(p).name for d in os.environ.get("PATH", "").split(":") for p in glob.glob(f"{d}/g++*")})
    for name in gcc_names:
        if not re.fullmatch(r"g\+\+(-\d+)?", name):
            continue
        found = version(run(name, "-dumpfullversion"))
        if found and found[0] >= MIN_GCC:
            usable.append((name.replace("g++", "gcc"), name))
            report.ok(f"{name} {dotted(found)}")

    stdlib = libstdcxx_major()
    sanitizing: list[tuple[str, str]] = []
    clang_names = sorted(
        {Path(p).name for d in os.environ.get("PATH", "").split(":") for p in glob.glob(f"{d}/clang++*")}
    )
    for name in clang_names:
        if not re.fullmatch(r"clang\+\+(-\d+)?", name):
            continue
        found = version(run(name, "--version"))
        if not found or found[0] < MIN_CLANG:
            continue
        if stdlib < MIN_LIBSTDCXX:
            report.warn(f"{name} {dotted(found)} with libstdc++ {stdlib}", f"install g++-{MIN_GCC} for libstdc++ 14")
            continue
        usable.append((name.replace("clang++", "clang"), name))
        if has_sanitizer_runtime(name):
            sanitizing.append((name.replace("clang++", "clang"), name))
            report.ok(f"{name} {dotted(found)}, libstdc++ {stdlib}, sanitizer runtime")
        else:
            report.ok(f"{name} {dotted(found)}, libstdc++ {stdlib}, no sanitizer runtime")

    if not usable:
        report.fail(
            "no C++23 compiler (the system's default may be GCC 13 or Clang 18)",
            f"install gcc-{MIN_GCC} g++-{MIN_GCC}",
            [f"gcc-{MIN_GCC}", f"g++-{MIN_GCC}"],
        )
        return
    cc, cxx = usable[0]
    print(f"           configure with: CC={cc} CXX={cxx} cmake --preset linux-debug (also linux-release, linux-coverage)")
    if sanitizing:
        cc, cxx = sanitizing[0]
        print(f"           configure with: CC={cc} CXX={cxx} cmake --preset linux-asan (also linux-tsan)")
    else:
        report.warn(
            "no Clang with the sanitizer runtime (compiler-rt); linux-asan and linux-tsan need one",
            "install clang-20 from apt.llvm.org, as CI does",
        )


def check_dev_packages(report: Report) -> None:
    section("Development headers (SDL3's X11 and Wayland back ends)")
    if shutil.which("pkg-config") is None:
        # Assume all of them absent, so the one apt-get line still covers every package; installing
        # one that is already there does nothing.
        packages = list(dict.fromkeys(DEV_PACKAGES.values()))
        report.fail(
            "the headers cannot be checked without pkg-config",
            f"install pkg-config and {' '.join(packages)}",
            ["pkg-config", *packages],
        )
        return
    absent = [module for module in DEV_PACKAGES if run("pkg-config", "--exists", module) is None]
    for module in absent:
        report.fail(f"{module} (pkg-config)", f"install {DEV_PACKAGES[module]}", [DEV_PACKAGES[module]])
    if not absent:
        report.ok(f"all {len(DEV_PACKAGES)} found by pkg-config")


def check_vcpkg(report: Report) -> None:
    section("vcpkg")
    baseline = json.loads((ROOT / "vcpkg.json").read_text())["builtin-baseline"]
    env_root = os.environ.get("VCPKG_ROOT")
    root = Path(env_root) if env_root else Path.home() / "vcpkg"
    if not (root / "scripts/buildsystems/vcpkg.cmake").exists():
        where = f"VCPKG_ROOT={env_root}" if env_root else "~/vcpkg"
        report.fail(
            f"no vcpkg checkout at {where}",
            f"git clone https://github.com/microsoft/vcpkg {root} && {root}/bootstrap-vcpkg.sh -disableMetrics",
        )
        return
    if not (root / "vcpkg").exists():
        report.fail(f"vcpkg at {root} is not bootstrapped", f"{root}/bootstrap-vcpkg.sh -disableMetrics")
    else:
        report.ok(f"vcpkg at {root}")
    if run("git", "-C", str(root), "cat-file", "-e", f"{baseline}^{{commit}}") is None:
        report.fail(
            f"the checkout does not have the manifest's baseline {baseline[:10]}",
            f"git -C {root} pull && {root}/bootstrap-vcpkg.sh -disableMetrics",
        )
    else:
        report.ok(f"baseline {baseline[:10]} present")
    if not env_root:
        report.warn(
            "VCPKG_ROOT is not set; the presets read it",
            f"export VCPKG_ROOT={root} in the shell's startup file, or set it on each cmake --preset",
        )


def icd_version(manifest: str) -> tuple[int, ...]:
    try:
        return version(json.loads(Path(manifest).read_text())["ICD"]["api_version"])
    except (OSError, KeyError, ValueError):
        return ()


def layer_dirs() -> list[str]:
    dirs: list[str] = []
    for variable in ("VK_LAYER_PATH", "VK_ADD_LAYER_PATH"):
        dirs += [d for d in os.environ.get(variable, "").split(":") if d]
    data_home = os.environ.get("XDG_DATA_HOME", str(Path.home() / ".local/share"))
    data_dirs = os.environ.get("XDG_DATA_DIRS", "/usr/local/share:/usr/share").split(":")
    dirs += [f"{d}/vulkan/explicit_layer.d" for d in [data_home, *data_dirs, "/etc"] if d]
    return dirs


def check_vulkan(report: Report) -> None:
    section("Vulkan (the loader, Lavapipe for the tests, a GPU for the editor)")
    # ldconfig is under /sbin, which a user's PATH may leave out.
    ldconfig = shutil.which("ldconfig") or "/sbin/ldconfig"
    if "libvulkan.so.1" not in (run(ldconfig, "-p") or ""):
        report.fail("no system Vulkan loader (libvulkan.so.1)", "install libvulkan1", ["libvulkan1"])
    else:
        report.ok("system loader libvulkan.so.1")

    icds = sorted(set(glob.glob("/usr/share/vulkan/icd.d/*.json") + glob.glob("/etc/vulkan/icd.d/*.json")))
    # Ubuntu installs one manifest per architecture (lvp_icd.i686.json sorts before
    # lvp_icd.x86_64.json); a 32-bit driver gives a 64-bit test no device.
    lavapipe = [
        icd
        for icd in icds
        if Path(icd).name in ("lvp_icd.json", f"lvp_icd.{platform.machine()}.json")
    ]
    if not lavapipe:
        report.fail(
            "no Lavapipe driver; the GPU tests run on it as CI does",
            "install mesa-vulkan-drivers",
            ["mesa-vulkan-drivers"],
        )
    else:
        found = icd_version(lavapipe[0])
        if found and found[:2] < MIN_VULKAN:
            report.fail(
                f"Lavapipe offers Vulkan {dotted(found)}, 1.4 needed",
                "sudo apt-get update && sudo apt-get install --only-upgrade mesa-vulkan-drivers (Ubuntu 24.04.5 has 1.4); else ppa:kisak/kisak-mesa, as CI",
            )
        else:
            report.ok(f"Lavapipe, Vulkan {dotted(found)}: VK_DRIVER_FILES={lavapipe[0]}")

    # A driver manifest is installed whether or not its hardware is present, so the GPUs are
    # asked of the loader instead.
    # vulkaninfo exits non-zero when one driver fails to load, yet still lists the others.
    status = run_status("vulkaninfo", "--summary") if shutil.which("vulkaninfo") else None
    summary = status[1] if status else None
    if not shutil.which("vulkaninfo"):
        report.warn("cannot list the GPUs without vulkaninfo", "install vulkan-tools")
    elif not summary or "deviceName" not in summary:
        last = (summary or "no output").strip().splitlines()[-1:] or ["no output"]
        report.warn(f"vulkaninfo --summary failed: {last[0]}", "run vulkaninfo --summary and read its errors")
    else:
        gpus = re.findall(r"apiVersion\s*=\s*(\S+).*?deviceType\s*=\s*(\S+).*?deviceName\s*=\s*([^\n]+)", summary, re.S)
        hardware = [(api, name.strip()) for api, kind, name in gpus if "CPU" not in kind]
        for api, name in hardware:
            found = version(api)
            if found[:2] >= MIN_VULKAN:
                report.ok(f"GPU {name}, Vulkan {dotted(found)}")
            else:
                report.warn(
                    f"GPU {name}, Vulkan {dotted(found)}: below the engine's 1.4",
                    "a newer driver; meanwhile the editor runs on Lavapipe with VK_DRIVER_FILES",
                )
        if not hardware:
            report.warn("no GPU besides the CPU drivers", "the editor runs on Lavapipe, slowly, with VK_DRIVER_FILES")

    layers = [
        path
        for d in layer_dirs()
        for path in glob.glob(f"{d}/*.json")
        if "khronos_validation" in Path(path).name.lower()
    ]
    if not layers:
        report.warn(
            "no Khronos validation layer; the engine runs without validation and says so in its log",
            "install the LunarG Vulkan SDK and source its setup-env.sh (a distribution's layer may be older than 1.4)",
        )
    else:
        try:
            found = version(json.loads(Path(layers[0]).read_text())["layer"]["api_version"])
        except (OSError, KeyError, ValueError):
            found = ()
        if found and found[:2] < MIN_VULKAN:
            report.warn(
                f"validation layer {dotted(found)} at {layers[0]} predates Vulkan 1.4",
                "install the LunarG Vulkan SDK and source its setup-env.sh",
            )
        else:
            report.ok(f"validation layer {dotted(found)}")


def check_contributing(report: Report) -> None:
    section("Running the editor and contributing")
    if os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY"):
        report.ok("a display for the editor")
    else:
        report.warn(
            "no DISPLAY or WAYLAND_DISPLAY; the editor cannot open, the tests still run",
            "run the editor from a desktop session",
        )
    formatter = shutil.which(f"clang-format-{CI_CLANG_FORMAT}") or shutil.which("clang-format")
    found = version(run(formatter, "--version")) if formatter else ()
    if not found:
        report.warn(
            f"no clang-format; CI rejects unformatted code with clang-format {CI_CLANG_FORMAT}",
            f"install clang-format-{CI_CLANG_FORMAT} (apt.llvm.org)",
        )
    elif found[0] != CI_CLANG_FORMAT:
        report.warn(
            f"clang-format {dotted(found)}; CI formats with {CI_CLANG_FORMAT}, which may disagree",
            f"install clang-format-{CI_CLANG_FORMAT} (apt.llvm.org)",
        )
    else:
        report.ok(f"clang-format {dotted(found)}")
    if shutil.which("gcovr"):
        report.ok("gcovr, for linux-coverage")
    else:
        report.warn("no gcovr; configuring linux-coverage fails without it", "pip install --user gcovr")
    # Relative to the repository, or absolute in a worktree; joining handles both.
    hook = ROOT / (run("git", "-C", str(ROOT), "rev-parse", "--git-path", "hooks/commit-msg") or "").strip()
    if hook.is_file():
        report.ok("commit-msg hook")
    else:
        report.warn("no commit-msg hook for Conventional Commits", "sh tools/install_hooks.sh")


def preset_triplets() -> dict[str, str]:
    """Each configure preset's VCPKG_TARGET_TRIPLET, following `inherits`."""
    try:
        presets = {p["name"]: p for p in json.loads((ROOT / "CMakePresets.json").read_text())["configurePresets"]}
    except (OSError, KeyError, ValueError):
        return {}

    def triplet(name: str) -> str | None:
        preset = presets.get(name, {})
        own = preset.get("cacheVariables", {}).get("VCPKG_TARGET_TRIPLET")
        if own:
            return own
        parents = preset.get("inherits", [])
        for parent in [parents] if isinstance(parents, str) else parents:
            if found := triplet(parent):
                return found
        return None

    return {name: found for name in presets if (found := triplet(name))}


def check_build_dirs() -> None:
    caches = sorted((ROOT / "build").glob("*/CMakeCache.txt"))
    if not caches:
        return
    section("Existing build directories (reconfigure with the same compiler, or vcpkg rebuilds every port)")
    triplets = preset_triplets()
    for cache in caches:
        values = dict(re.findall(r"^(CMAKE_CXX_COMPILER|VCPKG_TARGET_TRIPLET):[A-Z]+=(.*)$", cache.read_text(), re.M))
        name = cache.parent.name
        line = f"  build/{name}: {values.get('CMAKE_CXX_COMPILER', 'no compiler recorded')}"
        expected = triplets.get(name)
        if expected and values.get("VCPKG_TARGET_TRIPLET") not in (None, expected):
            line += f"; triplet {values['VCPKG_TARGET_TRIPLET']}, the preset's is {expected}: configure with --fresh"
        print(line)


def main() -> int:
    report = Report()
    if check_system(report):
        check_tools(report)
        check_compilers(report)
        check_dev_packages(report)
        check_vcpkg(report)
        check_vulkan(report)
        check_contributing(report)
        check_build_dirs()

    print()
    if report.apt:
        packages = " ".join(dict.fromkeys(report.apt))
        print(f"Ubuntu and Debian packages to install:\n  sudo apt-get install -y {packages}\n")
    print(f"{report.missing} missing, {report.warnings} warnings")
    return 1 if report.missing else 0


if __name__ == "__main__":
    sys.exit(main())
