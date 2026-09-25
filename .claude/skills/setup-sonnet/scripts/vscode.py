#!/usr/bin/env python3
"""Writes a VS Code configuration for one configured Linux preset into .vscode/ (gitignored).

tasks.json configures, builds and tests the way the setup did; launch.json runs the editor on the
basic sample and a module's test binary under gdb; settings.json points IntelliSense at the
preset's compile_commands.json and keeps CMake Tools from reconfiguring on open; extensions.json
recommends the C/C++ extension that provides the debugger.

The compiler comes from build/<preset>/CMakeCache.txt, so the preset has to be configured first.
A file that already exists and differs is left alone and its diff printed, unless --force.
"""

from __future__ import annotations

import argparse
import difflib
import json
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
SANITIZER_PRESETS = ("linux-asan", "linux-tsan")


def cache_value(cache: str, name: str) -> str | None:
    match = re.search(rf"^{name}:[A-Z]+=(.*)$", cache, re.M)
    return match.group(1) if match else None


def test_preset_env(preset: str) -> dict[str, str]:
    presets = json.loads((ROOT / "CMakePresets.json").read_text())
    for test in presets.get("testPresets", []):
        if test["name"] == preset:
            return {k: v.replace("${sourceDir}", "${workspaceFolder}") for k, v in test.get("environment", {}).items()}
    return {}


def key_values(pairs: list[str]) -> dict[str, str]:
    result = {}
    for pair in pairs:
        key, sep, value = pair.partition("=")
        if not sep or not key:
            sys.exit(f"expected KEY=VALUE, got {pair!r}")
        result[key] = value
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--preset", required=True, help="a configured Linux preset, e.g. linux-debug")
    parser.add_argument("--icd", required=True, help="the Lavapipe manifest the tests run on")
    parser.add_argument("--vcpkg-root", default=os.environ.get("VCPKG_ROOT"), help="default: $VCPKG_ROOT")
    parser.add_argument("--env", action="append", default=[], metavar="KEY=VALUE",
                        help="for every task and launch, e.g. the Vulkan SDK's VK_ADD_LAYER_PATH")
    parser.add_argument("--editor-env", action="append", default=[], metavar="KEY=VALUE",
                        help="for the editor launch only, over --env")
    parser.add_argument("--force", action="store_true", help="overwrite files that differ")
    args = parser.parse_args()

    build = ROOT / "build" / args.preset
    cache_path = build / "CMakeCache.txt"
    if not cache_path.exists():
        sys.exit(f"{cache_path.relative_to(ROOT)} not found: configure the preset first")
    if not args.vcpkg_root:
        sys.exit("VCPKG_ROOT is not set: pass --vcpkg-root")
    cache = cache_path.read_text()
    cc, cxx = cache_value(cache, "CMAKE_C_COMPILER"), cache_value(cache, "CMAKE_CXX_COMPILER")
    if not cc or not cxx:
        sys.exit(f"no compiler recorded in {cache_path.relative_to(ROOT)}")

    preset = args.preset
    out = f"${{workspaceFolder}}/build/{preset}"
    modules = sorted(p.parent.name for p in (ROOT / "modules").glob("*/tests") if p.is_dir())
    common = key_values(args.env)
    tests_env = {**common, "VK_DRIVER_FILES": args.icd, **test_preset_env(preset)}
    editor_env = {**common, **key_values(args.editor_env)}
    # The editor's own code is what gets instrumented; the drivers it loads are not (SKILL.md, step 4).
    if preset == "linux-tsan":
        editor_env.setdefault("VK_DRIVER_FILES", args.icd)

    module_input = {
        "id": "module",
        "type": "pickString",
        "description": "Module whose tests to run",
        "options": modules,
        "default": "core",
    }
    filter_input = {
        "id": "filter",
        "type": "promptString",
        "description": "Catch2 test spec: a test name, a [tag], or empty for all",
        "default": "",
    }

    tasks = {
        "version": "2.0.0",
        "options": {"env": {"VCPKG_ROOT": args.vcpkg_root, **common}},
        "tasks": [
            {
                "label": "configure",
                "type": "shell",
                "command": f"cmake --preset {preset}",
                "options": {"env": {"CC": cc, "CXX": cxx, "VCPKG_ROOT": args.vcpkg_root, **common}},
                "problemMatcher": [],
            },
            {
                "label": "build",
                "type": "shell",
                "command": f"cmake --build --preset {preset}",
                "group": {"kind": "build", "isDefault": True},
                "problemMatcher": {"base": "$gcc", "fileLocation": ["autoDetect", "${workspaceFolder}"]},
            },
            {
                "label": "test",
                "type": "shell",
                "command": f"ctest --preset {preset} -j$(nproc)",
                "options": {"env": tests_env},
                "dependsOn": "build",
                "group": {"kind": "test", "isDefault": True},
                "problemMatcher": [],
            },
            {
                "label": "test one module",
                "type": "shell",
                "command": f"ctest --preset {preset} -R '^${{input:module}}_tests$'",
                "options": {"env": tests_env},
                "dependsOn": "build",
                "group": "test",
                "problemMatcher": [],
            },
        ],
        "inputs": [module_input],
    }

    gdb = {
        "type": "cppdbg",
        "request": "launch",
        "MIMode": "gdb",
        "cwd": "${workspaceFolder}",
        "preLaunchTask": "build",
        "setupCommands": [
            {"description": "Pretty-print the standard library", "text": "-enable-pretty-printing", "ignoreFailures": True},
        ],
    }

    def env_list(env: dict[str, str]) -> list[dict[str, str]]:
        return [{"name": k, "value": v} for k, v in env.items()]

    launch = {
        "version": "0.2.0",
        "configurations": [
            {
                "name": "editor: basic sample",
                **gdb,
                "program": f"{out}/apps/editor/sonnet_editor",
                "args": ["apps/samples/basic"],
                "environment": env_list(editor_env),
            },
            {
                "name": "tests: one module",
                **gdb,
                "program": f"{out}/modules/${{input:module}}/${{input:module}}_tests",
                "args": ["${input:filter}"],
                "environment": env_list(tests_env),
            },
        ],
        "inputs": [module_input, filter_input],
    }

    settings = {
        "C_Cpp.default.compileCommands": f"{out}/compile_commands.json",
        "clangd.arguments": [f"--compile-commands-dir={out}"],
        # CMake Tools reconfigures on open with the window's environment, which from a desktop
        # launcher has no VCPKG_ROOT or CC; a different compiler rebuilds every vcpkg port.
        "cmake.configureOnOpen": False,
        "cmake.useCMakePresets": "always",
        "cmake.environment": {"VCPKG_ROOT": args.vcpkg_root},
        "cmake.configureEnvironment": {"CC": cc, "CXX": cxx},
    }

    extensions = {"recommendations": ["ms-vscode.cpptools"]}

    target = ROOT / ".vscode"
    target.mkdir(exist_ok=True)
    kept = 0
    for name, content in (("tasks.json", tasks), ("launch.json", launch), ("settings.json", settings),
                          ("extensions.json", extensions)):
        path = target / name
        text = json.dumps(content, indent=2) + "\n"
        if path.exists():
            old = path.read_text()
            if old == text:
                print(f"  same     .vscode/{name}")
                continue
            if not args.force:
                kept += 1
                print(f"  kept     .vscode/{name}: it differs, --force replaces it")
                sys.stdout.writelines(difflib.unified_diff(old.splitlines(True), text.splitlines(True),
                                                           f"a/.vscode/{name}", f"b/.vscode/{name}"))
                continue
        path.write_text(text)
        print(f"  wrote    .vscode/{name}")
    print(f"\npreset {preset}, CC={cc} CXX={cxx}, VCPKG_ROOT={args.vcpkg_root}, tests on {args.icd}")
    return 1 if kept else 0


if __name__ == "__main__":
    sys.exit(main())
