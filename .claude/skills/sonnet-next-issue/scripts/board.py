#!/usr/bin/env python3
"""Read and update the "Sonnet Issue Prioritization" board (project 1 of Pacheco95).

  board.py next                  print the top-most Ready for dev card as JSON (board order)
  board.py status N "In Progress"   set issue N's Status; the name is looked up live

Field and option ids are fetched at run time, so nothing here goes stale when the
board is edited. Needs `gh auth refresh -s project,read:project` once.
"""
import json
import subprocess
import sys

OWNER, NUMBER, REPO = "Pacheco95", "1", "Pacheco95/sonnet"


def gh(*args):
    out = subprocess.run(["gh", *args], check=True, capture_output=True, text=True)
    return json.loads(out.stdout) if out.stdout.strip() else None


def items():
    return gh("project", "item-list", NUMBER, "--owner", OWNER, "--limit", "100", "--format", "json")["items"]


def next_card():
    # item-list returns the cards in the order of the board's manual sorting, top first.
    for item in items():
        if item.get("status") == "Ready for dev" and item["content"].get("type") == "Issue":
            c = item["content"]
            blocked = gh("api", f"repos/{REPO}/issues/{c['number']}/dependencies/blocked_by") or []
            return {
                "number": c["number"],
                "title": c["title"],
                "url": c["url"],
                "priority_tier": item.get("priority Tier"),
                "effort_hours": item.get("effort (hours)"),
                "target_release": item.get("target Release"),
                "open_blockers": [b["number"] for b in blocked if b.get("state") == "open"],
            }
    return None


def set_status(number, name):
    project = gh("project", "view", NUMBER, "--owner", OWNER, "--format", "json")["id"]
    field = next(f for f in gh("project", "field-list", NUMBER, "--owner", OWNER, "--format", "json")["fields"]
                 if f["name"] == "Status")
    option = next((o["id"] for o in field["options"] if o["name"] == name), None)
    if option is None:
        sys.exit(f"no Status option {name!r}; have {[o['name'] for o in field['options']]}")
    item = next((i for i in items() if i["content"].get("number") == number), None)
    if item is None:
        sys.exit(f"issue #{number} is not on the board")
    subprocess.run(["gh", "project", "item-edit", "--id", item["id"], "--project-id", project,
                    "--field-id", field["id"], "--single-select-option-id", option], check=True)
    print(f"#{number} -> {name}")


if __name__ == "__main__":
    if sys.argv[1:2] == ["next"]:
        print(json.dumps(next_card(), indent=2))
    elif sys.argv[1:2] == ["status"] and len(sys.argv) == 4:
        set_status(int(sys.argv[2]), sys.argv[3])
    else:
        sys.exit(__doc__)
