#!/usr/bin/env python3
"""
Reorder a GitHub Project board so the priorities come first (top).

Usage:
  python reorder_project.py --project-owner OWNER --project-number NUMBER [--dry-run]

Order, top to bottom:
  1. Open items by Priority Tier: Quick Win, Medium Fit, Deferred, then items with no tier
  2. Within a tier, smaller Effort (hours) first, then the lower issue number
  3. Done items last, in the same order

The board's own Table view may sort by a field and hide this order; clear its sort to see it.

Project and item IDs are fetched at runtime. GitHub CLI must be authenticated.
"""

import argparse
import json
import subprocess
import sys

TIERS = {"Quick Win": 0, "Medium Fit": 1, "Deferred": 2}

MOVE = """
mutation($project: ID!, $item: ID!, $after: ID) {
  updateProjectV2ItemPosition(input: {projectId: $project, itemId: $item, afterId: $after}) {
    items(first: 1) { nodes { id } }
  }
}
"""


def gh(*args: str) -> str:
    result = subprocess.run(["gh", *args], capture_output=True, text=True)
    if result.returncode != 0:
        print(f"gh {' '.join(args[:2])} failed: {result.stderr.strip()[:300]}", file=sys.stderr)
        sys.exit(1)
    return result.stdout


def sort_key(item: dict) -> tuple:
    return (
        item.get("status") == "Done",
        TIERS.get(item.get("priority Tier"), len(TIERS)),
        item.get("effort (hours)") or 0,
        item["content"].get("number", 0),
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--project-owner", required=True)
    parser.add_argument("--project-number", type=int, required=True)
    parser.add_argument("--dry-run", action="store_true", help="print the order without moving anything")
    args = parser.parse_args()

    owner, number = args.project_owner, str(args.project_number)
    project_id = json.loads(gh("project", "view", number, "--owner", owner, "--format", "json"))["id"]
    # Items added a moment ago can be missing from the first listing, so add issues to the board
    # before running this, not in the same breath.
    items = json.loads(gh("project", "item-list", number, "--owner", owner, "--limit", "500", "--format", "json"))["items"]
    items = [i for i in items if i.get("content", {}).get("number")]
    ordered = sorted(items, key=sort_key)

    previous = None
    for item in ordered:
        label = f"#{item['content']['number']} {item.get('status')} {item.get('priority Tier')} {item.get('effort (hours)')}"
        if args.dry_run:
            print(label)
            continue
        command = ["api", "graphql", "-f", f"query={MOVE}", "-f", f"project={project_id}", "-f", f"item={item['id']}"]
        if previous:
            command += ["-f", f"after={previous}"]
        gh(*command)
        previous = item["id"]
        print(label)
    print(f"\n{'Would order' if args.dry_run else 'Ordered'} {len(ordered)} items.")


if __name__ == "__main__":
    main()
