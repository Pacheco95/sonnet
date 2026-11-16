"""Warn when `feat` or breaking commits have landed since the last engine version change.

docs/conventions.md ("Versioning"): a `feat` bumps MINOR, and so does a breaking change before 1.0.0. Several
features may share one bump, so this never fails: it lists the commits and, under GitHub Actions, emits a
`::warning::` annotation. It always exits 0 unless git itself fails.

Usage: check_version_bump.py [REV]   (default HEAD; needs the full history, e.g. checkout fetch-depth: 0)
"""
import os
import re
import subprocess
import sys

VERSION = re.compile(r"^project\(sonnet\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)", re.M)
FEATURE = re.compile(r"^(feat(\([^)]*\))?!?|[a-z]+(\([^)]*\))?!): ")


def git(*args: str) -> str:
    return subprocess.run(["git", *args], check=True, capture_output=True, text=True).stdout


def version_at(rev: str) -> str | None:
    try:
        match = VERSION.search(git("show", f"{rev}:CMakeLists.txt"))
    except subprocess.CalledProcessError:
        return None
    return match.group(1) if match else None


def last_bump(rev: str) -> str | None:
    """The newest commit reachable from rev whose CMakeLists.txt version differs from its first parent's."""
    for commit in git("log", "--format=%H", "-G", r"project\(sonnet", rev, "--", "CMakeLists.txt").split():
        if version_at(commit) != version_at(f"{commit}^"):
            return commit
    return None


def main() -> int:
    rev = sys.argv[1] if len(sys.argv) > 1 else "HEAD"
    bump = last_bump(rev)
    if bump is None:
        print("check_version_bump: no version change found in the history (shallow clone?); nothing to check")
        return 0
    version = version_at(bump)
    log = git("log", "--no-merges", "--format=%h%x09%s%x09%b%x00", f"{bump}..{rev}")
    features = []
    for entry in filter(None, (e.strip("\n") for e in log.split("\0"))):
        short, subject, body = (entry.split("\t", 2) + [""])[:3]
        if FEATURE.match(subject) or "BREAKING CHANGE:" in body:
            features.append(f"{short} {subject}")
    if not features:
        print(f"OK: no feat or breaking commits since the bump to {version} ({git('rev-parse', '--short', bump).strip()})")
        return 0

    message = (
        f"{len(features)} feat or breaking commit(s) since the version became {version}; "
        "bump MINOR in CMakeLists.txt and vcpkg.json when this release is cut (docs/conventions.md, Versioning)"
    )
    print(message)
    for line in features:
        print(f"  {line}")
    if os.environ.get("GITHUB_ACTIONS") == "true":
        hashes = [line.split(" ", 1)[0] for line in features]
        listed = ", ".join(hashes[:10]) + (f" and {len(hashes) - 10} more" if len(hashes) > 10 else "")
        print(f"::warning title=Version bump::{message}: {listed}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
