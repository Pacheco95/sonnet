"""Fail when vcpkg.json does not mirror the version declared in the root CMakeLists.txt."""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

cmake = (ROOT / "CMakeLists.txt").read_text()
match = re.search(r"^project\(sonnet\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)", cmake, re.M)
if not match:
    print("CMakeLists.txt: could not find project(sonnet VERSION x.y.z)")
    sys.exit(1)
cmake_version = match.group(1)

manifest = json.loads((ROOT / "vcpkg.json").read_text())
vcpkg_version = manifest.get("version") or manifest.get("version-string") or manifest.get("version-semver")

if cmake_version != vcpkg_version:
    print(f"version mismatch: CMakeLists.txt says {cmake_version}, vcpkg.json says {vcpkg_version}")
    sys.exit(1)
print(f"OK: version {cmake_version}")
