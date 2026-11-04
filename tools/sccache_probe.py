"""TEMPORARY (#466): measure sccache in CI and check what invalidates it. Dropped before the PR merges.

Run after the job's normal build, with the sccache server running and the build folder configured
with the sccache launcher. Prints a table and appends it to $GITHUB_STEP_SUMMARY.

Usage: sccache_probe.py <preset> <phases...>   phases: warm header flags toolchain
"""
import json
import os
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
preset, phases = sys.argv[1], sys.argv[2:]
build = ROOT / "build" / preset
HEADER = ROOT / "modules/core/include/sonnet/core/Handle.h"
rows = []


def run(*cmd, **kw):
    print("+", " ".join(map(str, cmd)), flush=True)
    return subprocess.run(cmd, check=True, cwd=ROOT, **kw)


def stats():
    out = subprocess.run(["sccache", "--show-stats", "--stats-format", "json"], check=True,
                         capture_output=True, text=True).stdout
    s = json.loads(out).get("stats", {})
    count = lambda v: sum(v.get("counts", {}).values()) if isinstance(v, dict) else v
    result = {
        "requests": s.get("compile_requests"),
        "hits": count(s.get("cache_hits")),
        "misses": count(s.get("cache_misses")),
        "non-cacheable": s.get("requests_not_cacheable"),
        "writes": s.get("cache_writes"),
    }
    if None in result.values():
        print("unexpected stats JSON:", out, flush=True)
    return result


def phase(name, expect, *cmd):
    run("sccache", "--zero-stats", stdout=subprocess.DEVNULL)
    start = time.monotonic()
    run(*cmd)
    seconds = time.monotonic() - start
    s = stats()
    rows.append((name, f"{seconds:.0f}", s, expect))
    print(name, f"{seconds:.0f} s", s, "expected:", expect, flush=True)


def planned_compiles():
    out = subprocess.run(["ninja", "-C", str(build), "-n"], check=True, capture_output=True, text=True).stdout
    return len(re.findall(r"Building (?:C|CXX) object", out))


if "warm" in phases:
    run("cmake", "--build", str(build), "--target", "clean")
    phase("clean rebuild", "all hits", "cmake", "--build", "--preset", preset)

if "header" in phases:
    original = HEADER.read_text()
    HEADER.write_text(original + "\ninline constexpr int kSccacheProbe = 1;\n")
    n = planned_compiles()
    phase("Handle.h changed", f"{n} misses (ninja plans {n} compiles)", "cmake", "--build", "--preset", preset)
    HEADER.write_text(original)
    phase("Handle.h restored", f"{planned_compiles()} hits", "cmake", "--build", "--preset", preset)

if "flags" in phases:
    cache = (build / "CMakeCache.txt").read_text()
    flags = re.search(r"^CMAKE_CXX_FLAGS:STRING=(.*)$", cache, re.M).group(1)
    # sccache leaves MSVC's preprocessor flags out of the hash (the preprocessed source reflects them),
    # so an unused define hits there; /GS- changes code generation instead.
    probe = "/GS-" if os.name == "nt" else "-DSONNET_SCCACHE_PROBE"
    run("cmake", str(build), f"-DCMAKE_CXX_FLAGS={flags} {probe}")
    run("cmake", "--build", str(build), "--target", "clean")
    phase(f"{probe} added to CMAKE_CXX_FLAGS", "all C++ misses", "cmake", "--build", "--preset", preset)
    run("cmake", str(build), f"-DCMAKE_CXX_FLAGS={flags}")

if "toolchain" in phases:
    tmp = Path(tempfile.mkdtemp())
    src = tmp / "probe.cpp"
    src.write_text("int probe() { return 466; }\n")
    compilers = [["cl", "/nologo"], ["clang-cl", "/nologo"]] if os.name == "nt" else [["clang++-20"], ["clang++-18"], ["g++-14"]]
    for i, cc in enumerate(compilers):
        out = tmp / f"probe{i}.o"
        flag = [f"/Fo{out}", "/c"] if os.name == "nt" else ["-o", str(out), "-c"]
        phase(f"{cc[0]} first", "1 miss", "sccache", *cc, *flag, str(src))
        phase(f"{cc[0]} again", "1 hit", "sccache", *cc, *flag, str(src))

lines = ["| Phase | Seconds | Requests | Hits | Misses | Non-cacheable | Writes | Expected |", "|---|---|---|---|---|---|---|---|"]
for name, seconds, s, expect in rows:
    lines.append(f"| {name} | {seconds} | {s['requests']} | {s['hits']} | {s['misses']} | {s['non-cacheable']} | {s['writes']} | {expect} |")
table = "\n".join(lines)
print(table)
if "GITHUB_STEP_SUMMARY" in os.environ:
    with open(os.environ["GITHUB_STEP_SUMMARY"], "a") as f:
        f.write(f"\n### sccache probe, {preset}\n\n{table}\n")
