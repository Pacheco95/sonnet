#!/usr/bin/env python3
"""
Reorder a GitHub Project board so the priorities come first (top).

Usage:
  python reorder_project.py --project-owner OWNER --project-number NUMBER --repo REPO [--dry-run]

Order, top to bottom:
  1. The open issues' cards by Priority Tier: Quick Win, Medium Fit, Deferred, then cards with no tier
  2. Within a tier, smaller Effort (hours) first, then the lower issue number
  3. Blocked cards after every other open card, in the same order

Done cards are never moved: they stay below the open ones in the order they reached Done, the
newest first (sonnet-next-issue's board.py puts a card at the top when it moves it to Done). A
closed issue's card is not moved either, whatever its Status; it belongs in Done.

The cards are found from the issue side: each open issue of REPO, which OWNER owns, and its card on
this board, leaving out archived cards. The board's own listing (gh project item-list, and the
GraphQL items connection it reads) can leave out cards that are on the board, for hours, and a card
it leaves out would stay below the open ones. An open issue with no card on the board is named at
the end, to be added before running this again.

The board's own Table view may sort by a field and hide this order; clear its sort to see it.

Project and item IDs are fetched at runtime. GitHub CLI must be authenticated.
"""

import argparse
import json
import subprocess
import sys

TIERS = {"Quick Win": 0, "Medium Fit": 1, "Deferred": 2}

# Open work first, then cards parked as Blocked.
STATUS_RANK = {"Blocked": 1}

# A page of the repository's open issues, each with its cards on any board.
OPEN_ISSUES = """
query($owner: String!, $repo: String!, $after: String) {
  repository(owner: $owner, name: $repo) {
    issues(states: OPEN, first: 50, after: $after) {
      pageInfo { hasNextPage endCursor }
      nodes {
        number
        projectItems(first: 10, includeArchived: false) {
          nodes {
            id
            project { id }
            status: fieldValueByName(name: "Status") { ... on ProjectV2ItemFieldSingleSelectValue { name } }
            tier: fieldValueByName(name: "Priority Tier") { ... on ProjectV2ItemFieldSingleSelectValue { name } }
            effort: fieldValueByName(name: "Effort (hours)") { ... on ProjectV2ItemFieldNumberValue { number } }
          }
        }
      }
    }
  }
}
"""

MOVE = """
mutation($project: ID!, $item: ID!, $after: ID) {
  updateProjectV2ItemPosition(input: {projectId: $project, itemId: $item, afterId: $after}) {
    items(first: 1) { nodes { id } }
  }
}
"""


def gh(*args: str) -> str:
    result = subprocess.run(["gh", *args], capture_output=True, text=True, encoding="utf-8")
    if result.returncode != 0:
        print(f"gh {' '.join(args[:2])} failed: {result.stderr.strip()[:300]}", file=sys.stderr)
        sys.exit(1)
    return result.stdout


def open_issue_cards(owner: str, repo: str, project_id: str) -> tuple:
    """The cards of the repository's open issues on the board, and the open issues that have none."""
    cards, missing = [], []
    after = None
    while True:
        command = ["api", "graphql", "-f", f"query={OPEN_ISSUES}", "-f", f"owner={owner}", "-f", f"repo={repo}"]
        if after:
            command += ["-f", f"after={after}"]
        issues = json.loads(gh(*command))["data"]["repository"]["issues"]
        for issue in issues["nodes"]:
            items = [item for item in issue["projectItems"]["nodes"] if item["project"]["id"] == project_id]
            if not items:
                missing.append(issue["number"])
            for item in items:
                cards.append({
                    "id": item["id"],
                    "number": issue["number"],
                    "status": (item["status"] or {}).get("name"),
                    "tier": (item["tier"] or {}).get("name"),
                    "effort": (item["effort"] or {}).get("number"),
                })
        if not issues["pageInfo"]["hasNextPage"]:
            return cards, missing
        after = issues["pageInfo"]["endCursor"]


def sort_key(card: dict) -> tuple:
    return (
        STATUS_RANK.get(card["status"], 0),
        TIERS.get(card["tier"], len(TIERS)),
        card["effort"] or 0,
        card["number"],
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--project-owner", required=True)
    parser.add_argument("--project-number", type=int, required=True)
    parser.add_argument("--repo", required=True, help="the repository's name only (sonnet, not owner/sonnet)")
    parser.add_argument("--dry-run", action="store_true", help="print the order without moving anything")
    args = parser.parse_args()
    if "/" in args.repo:
        parser.error("--repo takes the repository's name only; its owner is --project-owner")

    owner, number = args.project_owner, str(args.project_number)
    project_id = json.loads(gh("project", "view", number, "--owner", owner, "--format", "json"))["id"]
    cards, missing = open_issue_cards(owner, args.repo, project_id)
    # Done cards keep their place; moving every open card to the top leaves them below, in order.
    ordered = sorted((card for card in cards if card["status"] != "Done"), key=sort_key)

    previous = None
    for card in ordered:
        effort = "None" if card["effort"] is None else f"{card['effort']:g}"
        label = f"#{card['number']} {card['status']} {card['tier']} {effort}"
        if args.dry_run:
            print(label)
            continue
        command = ["api", "graphql", "-f", f"query={MOVE}", "-f", f"project={project_id}", "-f", f"item={card['id']}"]
        if previous:
            command += ["-f", f"after={previous}"]
        gh(*command)
        previous = card["id"]
        print(label)
    print(f"\n{'Would order' if args.dry_run else 'Ordered'} {len(ordered)} items.")
    if missing:
        print("Open issues with no card on the board: " + ", ".join(f"#{n}" for n in sorted(missing)))


if __name__ == "__main__":
    main()
