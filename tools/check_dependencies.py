"""Verify the one-way module dependency rule (docs/architecture.md, "Dependency rule").

The order of `add_subdirectory` calls in modules/CMakeLists.txt is the canonical dependency order. A
module may include headers from, and link, only itself and modules listed before it. CMake only
rejects cycles, so a forward link that is not a cycle, and any forward `#include`, would otherwise
pass. The player and the cook tool also never reach the editor-only modules.

With `--graphviz FILE.dot`, the edges of the configured CMake target graph (from
`cmake --graphviz=FILE.dot <build dir>`) are checked against the same order too, which sees links
made through variables and generator expressions that the source scan cannot.
"""
import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EDITOR_ONLY = {"ui", "editor"}
NO_EDITOR_APPS = ["player", "cook"]
SOURCE_SUFFIXES = {".h", ".hpp", ".cpp", ".mm", ".m", ".inl"}

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]sonnet/([a-z_]+)/', re.M)
LINK_RE = re.compile(r"\bsonnet::([a-z_]+)\b")


def module_order() -> list[str]:
    text = (ROOT / "modules/CMakeLists.txt").read_text()
    text = re.sub(r"#.*", "", text)
    return re.findall(r"add_subdirectory\(\s*([a-z_]+)\s*\)", text)


def cmake_links(path: Path) -> list[tuple[int, str]]:
    """Every `sonnet::<name>` reference outside comments, with its line number."""
    out = []
    for n, line in enumerate(path.read_text().splitlines(), 1):
        for name in LINK_RE.findall(line.split("#", 1)[0]):
            out.append((n, name))
    return out


DOT_EDGE_RE = re.compile(r"//\s+(\S+) -> (\S+)\s*$", re.M)
APP_TARGETS = {"sonnet_player_app": "player", "sonnet_cook_app": "cook", "sonnet_editor_app": "editor app"}


def target_module(target: str, rank: dict[str, int]) -> str | None:
    """The module a CMake target belongs to: sonnet_<m>, sonnet_<m>_<part> (rhi_vulkan) or <m>_tests."""
    if target.endswith("_tests") and target[:-6] in rank:
        return target[:-6]
    if target.startswith("sonnet_"):
        rest = target[len("sonnet_"):]
        for m in sorted(rank, key=len, reverse=True):
            if rest == m or rest.startswith(m + "_"):
                return m
    return None


def check_graph(dot: Path, rank: dict[str, int]) -> list[str]:
    problems = []
    edges = DOT_EDGE_RE.findall(dot.read_text())
    if not edges:
        return [f"GRAPH  no edges found in {dot}; was it made by `cmake --graphviz`?"]
    for src, dst in edges:
        dst_module = dst[len("sonnet_"):] if dst.startswith("sonnet_") and dst[len("sonnet_"):] in rank else None
        if dst_module is None:
            continue
        if src in APP_TARGETS:
            if src != "sonnet_editor_app" and dst_module in EDITOR_ONLY:
                problems.append(f"GRAPH  {src} links editor-only module `{dst_module}`")
            continue
        src_module = target_module(src, rank)
        if src_module and rank[dst_module] > rank[src_module]:
            problems.append(f"GRAPH  {src} links {dst}; `{dst_module}` comes later than `{src_module}` in the dependency order")
    return problems


def sources(base: Path):
    for p in sorted(base.rglob("*")):
        if p.is_file() and p.suffix in SOURCE_SUFFIXES:
            yield p


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--graphviz", type=Path, help="also check a .dot file made by `cmake --graphviz`")
    args = parser.parse_args()
    order = module_order()
    if not order:
        print("could not read the module order from modules/CMakeLists.txt")
        return 1
    rank = {m: i for i, m in enumerate(order)}
    problems: list[str] = []

    for m in order:
        mod = ROOT / "modules" / m
        if not mod.is_dir():
            problems.append(f"ORDER  modules/CMakeLists.txt lists `{m}` but modules/{m} does not exist")
            continue

        def check(kind: str, path: Path, line: int, dep: str):
            if dep in rank and rank[dep] > rank[m]:
                rel = path.relative_to(ROOT)
                problems.append(
                    f"UPWARD {rel}:{line} {kind} `{dep}` from `{m}`; `{dep}` comes later in the dependency order"
                )

        cmake = mod / "CMakeLists.txt"
        if cmake.exists():
            for line, dep in cmake_links(cmake):
                check("links", cmake, line, dep)
        for src in sources(mod):
            text = src.read_text(errors="replace")
            for match in INCLUDE_RE.finditer(text):
                check("includes", src, text.count("\n", 0, match.start()) + 1, match.group(1))

    for app in NO_EDITOR_APPS:
        base = ROOT / "apps" / app
        if not base.is_dir():
            continue
        files = [(p, INCLUDE_RE) for p in sources(base)]
        if (base / "CMakeLists.txt").exists():
            files.append((base / "CMakeLists.txt", None))
        for path, regex in files:
            if regex is None:
                hits = [(n, d) for n, d in cmake_links(path)]
            else:
                text = path.read_text(errors="replace")
                hits = [(text.count("\n", 0, mt.start()) + 1, mt.group(1)) for mt in regex.finditer(text)]
            for line, dep in hits:
                if dep in EDITOR_ONLY:
                    problems.append(
                        f"EDITOR {path.relative_to(ROOT)}:{line} apps/{app} reaches editor-only module `{dep}`"
                    )

    if args.graphviz:
        problems += check_graph(args.graphviz, rank)

    for p in problems:
        print(p)
    if problems:
        print(f"\n{len(problems)} dependency violation(s); see docs/architecture.md, \"Dependency rule\".")
        return 1
    print(f"dependency rule ok ({len(order)} modules: {' -> '.join(order)})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
