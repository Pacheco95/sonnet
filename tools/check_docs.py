"""Verify relative links, anchors and cross-document consistency of the Sonnet docs."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
md_files = sorted(p for p in ROOT.rglob("*.md") if not any(part.startswith(("build", ".")) for part in p.relative_to(ROOT).parent.parts))
problems: list[str] = []


def slug(heading: str) -> str:
    s = heading.strip().lower()
    s = re.sub(r"[^\w\- ]", "", s)
    return s.replace(" ", "-")


def headings(path: Path) -> set[str]:
    out = set()
    for line in path.read_text().splitlines():
        m = re.match(r"^#{1,6}\s+(.*)$", line)
        if m:
            out.add(slug(m.group(1)))
    return out


LINK_RE = re.compile(r"\]\(([^)\s]+)\)")
for f in md_files:
    text = f.read_text()
    for link in LINK_RE.findall(text):
        if link.startswith(("http://", "https://", "mailto:")):
            continue
        target, _, anchor = link.partition("#")
        tpath = f if not target else (f.parent / target)
        if not tpath.exists():
            problems.append(f"BROKEN LINK  {f.relative_to(ROOT)} -> {link}")
            continue
        if anchor and tpath.is_file() and anchor not in headings(tpath):
            problems.append(f"BAD ANCHOR   {f.relative_to(ROOT)} -> {link}")

readme = (ROOT / "README.md").read_text()
arch = (ROOT / "docs/architecture.md").read_text()
build = (ROOT / "docs/build.md").read_text()
roadmap = (ROOT / "docs/roadmap.md").read_text()

# Modules in the README diagram must appear in the architecture module table.
diagram = readme.split("```mermaid")[1].split("```")[0]
modules = sorted(set(re.findall(r"^\s*([a-z]+)(?:\s|$|\s*-->)", diagram, re.M)) - {"flowchart"})
for m in modules:
    if f"| `{m}`" not in arch:
        problems.append(f"MODULE       `{m}` in README diagram but not in docs/architecture.md table")

# Every vcpkg port named in the README technology table must be mentioned in docs/build.md.
ports = set()
for line in readme.splitlines():
    if line.startswith("|") and "vcpkg `" in line:
        ports.update(re.findall(r"vcpkg `([a-z0-9\-\[\],]+)`", line))
        ports.update(re.findall(r"`([a-z0-9\-]+)`", line.split("| vcpkg")[-1]))
for p in sorted(ports):
    base = p.split("[")[0]
    if base not in build:
        problems.append(f"PORT         `{base}` in README but not in docs/build.md")

# Every milestone referenced in the README features table must be a roadmap heading.
for ms in sorted(set(re.findall(r"\bM(\d)\b", readme))):
    if not re.search(rf"^## M{ms}:", roadmap, re.M):
        problems.append(f"MILESTONE    M{ms} referenced in README but no '## M{ms}:' heading in docs/roadmap.md")

# Every ADR listed in the README decisions table must exist and vice versa.
adr_dir = ROOT / "docs/decisions"
listed = set(re.findall(r"decisions/(\d{4}-[a-z0-9.\-]+\.md)", readme))
on_disk = {p.name for p in adr_dir.glob("0*.md")}
for missing in sorted(on_disk - listed):
    problems.append(f"ADR          {missing} exists but is not listed in README")

# Every capture flag the editor accepts must be in the Screenshots section of docs/editor.md, and
# AGENTS.md must point agents at it: agents and scripts are who the flags are for.
capture_source = (ROOT / "modules/editor/src/Capture.cpp").read_text()
flags = re.findall(r'^\s*\{"(--[a-z\-]+)"', capture_source, re.M)
if not flags:
    problems.append("CAPTURE      no flags found in modules/editor/src/Capture.cpp's option table")
editor_doc = (ROOT / "docs/editor.md").read_text()
screenshots = editor_doc.split("## Screenshots", 1)[1].split("\n## ", 1)[0] if "## Screenshots" in editor_doc else ""
if not screenshots:
    problems.append("CAPTURE      docs/editor.md has no '## Screenshots' section")
for flag in flags:
    if f"`{flag} " not in screenshots and f"`{flag}`" not in screenshots:
        problems.append(f"CAPTURE      {flag} is an editor flag but not in docs/editor.md, Screenshots")
agents = (ROOT / "AGENTS.md").read_text()
for needle in ("--screenshot", "--help", "docs/editor.md#screenshots"):
    if needle not in agents:
        problems.append(f"CAPTURE      AGENTS.md does not mention {needle}, which is how agents find the screenshots")

print(f"checked {len(md_files)} markdown files, modules={modules}, ports={len(ports)}, capture flags={len(flags)}")
if problems:
    print("\n".join(problems))
    sys.exit(1)
print("OK: no broken links, anchors, or consistency gaps")
