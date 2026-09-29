#!/usr/bin/env python3
"""
Map the dependencies between open issues, report cycles, and keep GitHub's native
"blocked by" links in step with what the issue texts say.

Usage (from the repository root):
  python3 .claude/skills/sonnet-issues/scripts/dependencies.py            # report only
  python3 .claude/skills/sonnet-issues/scripts/dependencies.py --apply    # add the missing links

An edge A -> B means "A is blocked by B". Two sources feed the graph:
  - GitHub's native blocked-by links (the source of truth), and
  - "Depends on #N" / "Blocked by #N" in an issue body (everything up to the end of the
    sentence, so "Depends on #66, #67 (step 2)." yields both).
Only edges between two open issues count; a closed blocker is already satisfied.

This never touches a card's Status. --apply only adds native links, and skips any link that
would close a cycle. Exit status: 0 clean, 2 when a cycle exists.
"""

import argparse
import json
import re
import subprocess
import sys

TEXT_EDGE = re.compile(r"(?i)\b(?:depends\s+on|blocked\s+by)\b([^.\n]*)")
ISSUE_REF = re.compile(r"#(\d+)")


def gh(*args):
    result = subprocess.run(["gh", *args], capture_output=True, text=True)
    if result.returncode != 0:
        sys.exit(f"gh {' '.join(args)} failed: {result.stderr.strip()}")
    return result.stdout


def find_cycles(edges):
    """Every strongly connected component with more than one issue, or a self-loop, with one
    concrete cycle through it."""
    index, low, on_stack, stack, comps = {}, {}, set(), [], []
    counter = [0]

    def visit(v):
        index[v] = low[v] = counter[0]
        counter[0] += 1
        stack.append(v)
        on_stack.add(v)
        for w in sorted(edges.get(v, ())):
            if w not in index:
                visit(w)
                low[v] = min(low[v], low[w])
            elif w in on_stack:
                low[v] = min(low[v], index[w])
        if low[v] == index[v]:
            comp = []
            while True:
                w = stack.pop()
                on_stack.discard(w)
                comp.append(w)
                if w == v:
                    break
            if len(comp) > 1 or v in edges.get(v, ()):
                comps.append(sorted(comp))

    sys.setrecursionlimit(10000)
    for v in sorted(edges):
        if v not in index:
            visit(v)

    cycles = []
    for comp in comps:
        members, start = set(comp), comp[0]
        path, seen = [start], {start}

        def walk(v):
            for w in sorted(edges.get(v, ())):
                if w == start:
                    return True
                if w in members and w not in seen:
                    seen.add(w)
                    path.append(w)
                    if walk(w):
                        return True
                    path.pop()
            return False

        walk(start)
        cycles.append(path + [start])
    return cycles


def creates_cycle(edges, blocked, blocker):
    """Would `blocked -> blocker` close a loop, i.e. does `blocker` already reach `blocked`?"""
    todo, seen = [blocker], set()
    while todo:
        v = todo.pop()
        if v == blocked:
            return True
        if v not in seen:
            seen.add(v)
            todo.extend(edges.get(v, ()))
    return False


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    parser.add_argument("--apply", action="store_true", help="add the missing native blocked-by links")
    args = parser.parse_args()

    repo = gh("repo", "view", "--json", "nameWithOwner", "-q", ".nameWithOwner").strip()
    issues = json.loads(gh("issue", "list", "--state", "open", "--limit", "200", "--json", "number,title,body"))
    titles = {i["number"]: i["title"] for i in issues}
    ids = {}

    native, text = {}, {}
    for issue in issues:
        n = issue["number"]
        ids[n] = json.loads(gh("api", f"repos/{repo}/issues/{n}"))["id"]
        blockers = json.loads(gh("api", f"repos/{repo}/issues/{n}/dependencies/blocked_by"))
        native[n] = {b["number"] for b in blockers if b["number"] in titles}
        found = set()
        for match in TEXT_EDGE.finditer(issue["body"] or ""):
            found |= {int(r) for r in ISSUE_REF.findall(match.group(1))}
        text[n] = {b for b in found if b in titles and b != n}

    edges = {n: native[n] | text[n] for n in titles}
    missing = sorted((n, b) for n in titles for b in text[n] - native[n])

    print("# Dependencies between open issues\n")
    print("Edges read `A is blocked by B`. Closed blockers are left out.\n")
    linked = sorted((n, b) for n in edges for b in edges[n])
    if not linked:
        print("None of the open issues depends on another.\n")
    for n, b in linked:
        source = "native" if b in native[n] else "text only"
        print(f"- #{n} {titles[n]} <- #{b} {titles[b]} ({source})")

    print("\n## Missing native links\n")
    if not missing:
        print("None: every dependency the texts state is a native link.")
    for n, b in missing:
        print(f"- #{n} says it depends on #{b}; no native link yet")

    cycles = find_cycles(edges)
    print("\n## Cycles\n")
    if not cycles:
        print("None.")
    for cycle in cycles:
        print("- CYCLE: " + " -> ".join(f"#{n}" for n in cycle))
        print("  Each arrow reads 'is blocked by'. Nothing on it can start; break one link.")

    if args.apply and missing:
        print("\n## Applying\n")
        for n, b in missing:
            if creates_cycle(edges, n, b):
                print(f"- skipped #{n} <- #{b}: it would close a cycle")
                continue
            gh("api", "-X", "POST", f"repos/{repo}/issues/{n}/dependencies/blocked_by", "-F", f"issue_id={ids[b]}")
            print(f"- #{n} is now blocked by #{b}")
    elif missing:
        print("\nRun with --apply to add the missing links (after the user approves).")

    return 2 if cycles else 0


if __name__ == "__main__":
    sys.exit(main())
