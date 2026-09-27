"""Prune a vcpkg `files` binary cache to the archives the current build folders use.

vcpkg names each archive after the SHA-256 of its port's vcpkg_abi_info.txt, as <cache>/<abi[0:2]>/<abi>.zip,
so the archives in use are the hashes of build/*/vcpkg_installed/*/share/*/vcpkg_abi_info.txt. Every other
archive is deleted. With --since <marker>, the script also reports whether vcpkg wrote any archive after the
marker, as `changed=true|false` on stdout and in $GITHUB_OUTPUT when CI sets it, which tells CI whether the
cache is worth saving.

Usage: prune_vcpkg_cache.py <cache-dir> [--since <marker-file>]
"""
import argparse
import hashlib
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

parser = argparse.ArgumentParser()
parser.add_argument("cache", type=Path)
parser.add_argument("--since", type=Path)
args = parser.parse_args()

abi_files = sorted(ROOT.glob("build/*/vcpkg_installed/*/share/*/vcpkg_abi_info.txt"))
if not abi_files:
    # Pruning against nothing would empty the cache; a configure that installed no ports is a mistake.
    print("no vcpkg_abi_info.txt under build/*/vcpkg_installed; refusing to prune")
    sys.exit(1)
used = {hashlib.sha256(f.read_bytes()).hexdigest() for f in abi_files}

since = args.since.stat().st_mtime if args.since else None
kept = removed = new = 0
for archive in sorted(args.cache.glob("*/*.zip")):
    if archive.stem not in used:
        archive.unlink()
        removed += 1
        continue
    kept += 1
    if since is not None and archive.stat().st_mtime > since:
        new += 1

missing = len(used) - kept
print(f"{len(used)} port builds in use: kept {kept} archives ({new} new), removed {removed}, {missing} not cached")
if args.since:
    changed = "true" if new else "false"
    print(f"changed={changed}")
    if "GITHUB_OUTPUT" in os.environ:
        with open(os.environ["GITHUB_OUTPUT"], "a") as out:
            out.write(f"changed={changed}\n")
