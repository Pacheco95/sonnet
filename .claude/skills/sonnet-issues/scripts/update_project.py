#!/usr/bin/env python3
"""
Update GitHub Project fields for prioritized issues.

Usage:
  python update_project.py --issues issues.json --project-owner OWNER --project-number NUMBER --repo REPO

Issues JSON format:
  [
    {
      "number": 68,
      "title": "Instantiate objects as children",
      "priority_tier": "Quick Win",
      "effort_hours": "1-2",
      "target_release": "Post-1.0",
      "status": "Ready for dev"        (optional)
    },
    ...
  ]

Status is optional. The script moves a card only between Backlog, Refining and Ready for dev
(or sets it when empty). A card In Progress, Blocked or Done is left where it is, and the
summary says so.

Security:
  - Project identifiers are passed as arguments (not hardcoded)
  - Field and option IDs are fetched dynamically from the project
  - No sensitive data is logged or cached
  - GitHub CLI must be authenticated before running

Reliability:
  - Direct GraphQL mutations with proper formatting
  - Comprehensive error handling and validation
  - Validates all prerequisites before updating
"""

import json
import re
import subprocess
import sys
import time
from pathlib import Path


def get_project_id(project_owner: str, project_number: int) -> str:
    """Dynamically fetch the project ID."""
    query = f"""
    query {{
      user(login: "{project_owner}") {{
        projectV2(number: {project_number}) {{
          id
        }}
      }}
    }}
    """
    result = subprocess.run(
        ["gh", "api", "graphql", "-f", f"query={query}"],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        return None

    try:
        data = json.loads(result.stdout)
        return data.get("data", {}).get("user", {}).get("projectV2", {}).get("id")
    except json.JSONDecodeError:
        return None


def get_field_ids(project_owner: str, project_number: int) -> dict:
    """Dynamically fetch field IDs from the project."""
    query = f"""
    query {{
      user(login: "{project_owner}") {{
        projectV2(number: {project_number}) {{
          fields(first: 20) {{
            nodes {{
              ... on ProjectV2SingleSelectField {{
                id
                name
              }}
              ... on ProjectV2Field {{
                id
                name
              }}
            }}
          }}
        }}
      }}
    }}
    """
    result = subprocess.run(
        ["gh", "api", "graphql", "-f", f"query={query}"],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        return {}

    try:
        data = json.loads(result.stdout)
        fields = data.get("data", {}).get("user", {}).get("projectV2", {}).get("fields", {}).get("nodes", [])

        field_ids = {}
        for field in fields:
            name = field.get("name")
            field_id = field.get("id")
            if name and field_id and name in ["Priority Tier", "Effort (hours)", "Target Release", "Status"]:
                field_ids[name] = field_id

        return field_ids
    except json.JSONDecodeError:
        return {}


def get_option_ids(project_owner: str, project_number: int) -> dict:
    """Dynamically fetch option IDs for single-select fields."""
    query = f"""
    query {{
      user(login: "{project_owner}") {{
        projectV2(number: {project_number}) {{
          fields(first: 20) {{
            nodes {{
              ... on ProjectV2SingleSelectField {{
                name
                options {{
                  id
                  name
                }}
              }}
            }}
          }}
        }}
      }}
    }}
    """
    result = subprocess.run(
        ["gh", "api", "graphql", "-f", f"query={query}"],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        return {}

    try:
        data = json.loads(result.stdout)
        fields = data.get("data", {}).get("user", {}).get("projectV2", {}).get("fields", {}).get("nodes", [])

        options = {}
        for field in fields:
            field_name = field.get("name")
            if field_name in ["Priority Tier", "Target Release", "Status"]:
                options[field_name] = {}
                for opt in field.get("options", []):
                    options[field_name][opt["name"]] = opt["id"]

        return options
    except json.JSONDecodeError:
        return {}


# Statuses this script may move a card out of, and into. In Progress, Blocked and Done belong to
# whoever develops the card.
MOVABLE_STATUSES = ("Backlog", "Refining", "Ready for dev")


def get_issue_project_item(project_owner: str, repo_name: str, issue_number: int) -> tuple:
    """Get the project item ID and current Status name (or None) for a GitHub issue."""
    query = f"""
    query {{
      repository(owner: "{project_owner}", name: "{repo_name}") {{
        issue(number: {issue_number}) {{
          projectItems(first: 1) {{
            nodes {{
              id
              fieldValueByName(name: "Status") {{
                ... on ProjectV2ItemFieldSingleSelectValue {{
                  name
                }}
              }}
            }}
          }}
        }}
      }}
    }}
    """
    result = subprocess.run(
        ["gh", "api", "graphql", "-f", f"query={query}"],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        return None, None

    try:
        data = json.loads(result.stdout)
        items = data.get("data", {}).get("repository", {}).get("issue", {}).get("projectItems", {}).get("nodes", [])
        if items:
            status = (items[0].get("fieldValueByName") or {}).get("name")
            return items[0]["id"], status
    except (json.JSONDecodeError, KeyError, IndexError):
        pass

    return None, None


def update_field(project_id: str, item_id: str, field_id: str, field_value: str, field_name: str) -> bool:
    """Update a single field on a project item using direct GraphQL mutation."""
    # Determine the value based on field name
    if field_name == "Effort (hours)":
        # Number field - a range ("6-8", "6–8") is stored as its rounded midpoint
        try:
            bounds = [float(part) for part in re.split(r"\s*[-\u2013\u2014]\s*", str(field_value).strip()) if part]
            effort = int(round(sum(bounds) / len(bounds) + 1e-9))
        except (ValueError, ZeroDivisionError):
            effort = 0
        mutation = f"""mutation {{
  updateProjectV2ItemFieldValue(input: {{
    projectId: "{project_id}"
    itemId: "{item_id}"
    fieldId: "{field_id}"
    value: {{number: {effort}}}
  }}) {{
    clientMutationId
  }}
}}"""
    else:
        # Single select field - field_value should already be the option ID
        mutation = f"""mutation {{
  updateProjectV2ItemFieldValue(input: {{
    projectId: "{project_id}"
    itemId: "{item_id}"
    fieldId: "{field_id}"
    value: {{singleSelectOptionId: "{field_value}"}}
  }}) {{
    clientMutationId
  }}
}}"""

    result = subprocess.run(
        ["gh", "api", "graphql", "-f", f"query={mutation}"],
        capture_output=True,
        text=True,
    )

    if result.returncode != 0:
        return False

    # Verify the mutation succeeded
    try:
        data = json.loads(result.stdout)
        return data.get("data", {}).get("updateProjectV2ItemFieldValue") is not None
    except json.JSONDecodeError:
        return False


def main():
    import argparse

    parser = argparse.ArgumentParser(
        description="Update GitHub Project fields for prioritized issues",
        epilog="Example: python update_project.py --issues summary.json --project-owner myuser --project-number 1 --repo myrepo"
    )
    parser.add_argument("--issues", required=True, help="Path to issues JSON file")
    parser.add_argument("--project-owner", required=True, help="GitHub project owner (username)")
    parser.add_argument("--project-number", type=int, required=True, help="GitHub project number")
    parser.add_argument("--repo", required=True, help="Repository name")

    args = parser.parse_args()

    # Validate inputs
    if not Path(args.issues).exists():
        print(f"Error: Issues file not found: {args.issues}", file=sys.stderr)
        sys.exit(1)

    with open(args.issues) as f:
        issues = json.load(f)

    if not issues:
        print("Error: No issues found in JSON file", file=sys.stderr)
        sys.exit(1)

    print("Fetching project configuration...")

    # Dynamically fetch project and field information
    project_id = get_project_id(args.project_owner, args.project_number)
    if not project_id:
        print("Error: Could not fetch project ID. Check owner and project number.", file=sys.stderr)
        sys.exit(1)

    field_ids = get_field_ids(args.project_owner, args.project_number)
    if not all(f in field_ids for f in ["Priority Tier", "Effort (hours)", "Target Release"]):
        print("Error: Not all required fields found in project.", file=sys.stderr)
        print(f"  Found: {list(field_ids.keys())}", file=sys.stderr)
        sys.exit(1)

    option_ids = get_option_ids(args.project_owner, args.project_number)
    if not option_ids.get("Priority Tier") or not option_ids.get("Target Release"):
        print("Error: Could not fetch option IDs from project.", file=sys.stderr)
        sys.exit(1)

    print(f"Updating {len(issues)} issues in the GitHub Project...\n")

    failed_count = 0

    for i, issue in enumerate(issues, 1):
        issue_num = issue["number"]
        print(f"[{i}/{len(issues)}] Updating #{issue_num}...", end=" ", flush=True)

        # Get the project item ID
        item_id, current_status = get_issue_project_item(args.project_owner, args.repo, issue_num)
        if not item_id:
            print("✗ (not in project)")
            failed_count += 1
            continue

        success = True

        # Update Priority Tier
        priority_tier = issue.get("priority_tier", "")
        priority_option_id = option_ids.get("Priority Tier", {}).get(priority_tier)
        if priority_tier and priority_option_id:
            if update_field(project_id, item_id, field_ids["Priority Tier"], priority_option_id, "Priority Tier"):
                print("✓", end=" ", flush=True)
            else:
                print("✗", end=" ", flush=True)
                success = False
        else:
            print("✗", end=" ", flush=True)
            success = False

        # Update Effort (hours)
        effort_hours = issue.get("effort_hours", "")
        if effort_hours:
            if update_field(project_id, item_id, field_ids["Effort (hours)"], effort_hours, "Effort (hours)"):
                print("✓", end=" ", flush=True)
            else:
                print("✗", end=" ", flush=True)
                success = False
        else:
            print("✗", end=" ", flush=True)
            success = False

        # Update Target Release
        target_release = issue.get("target_release", "")
        target_option_id = option_ids.get("Target Release", {}).get(target_release)
        if target_release and target_option_id:
            if update_field(project_id, item_id, field_ids["Target Release"], target_option_id, "Target Release"):
                print("✓", end="")
            else:
                print("✗", end="")
                success = False
        else:
            print("✗", end="")
            success = False

        # Move the card, when asked to and when it is still in the planning columns
        status = issue.get("status", "")
        if status:
            status_option_id = option_ids.get("Status", {}).get(status)
            if not status_option_id or "Status" not in field_ids:
                print(f"  ✗ status: no option {status!r}", end="")
                success = False
            elif status == current_status:
                print(f"  status {status} (unchanged)", end="")
            elif current_status is not None and current_status not in MOVABLE_STATUSES:
                print(f"  status stays {current_status}", end="")
            elif update_field(project_id, item_id, field_ids["Status"], status_option_id, "Status"):
                print(f"  status {current_status} -> {status} ✓", end="")
            else:
                print(f"  ✗ status {status}", end="")
                success = False
        print()

        if not success:
            failed_count += 1

        # Rate limiting: wait between updates
        if i < len(issues):
            time.sleep(0.3)

    print()
    success_count = len(issues) - failed_count
    print(f"Done! Updated {success_count}/{len(issues)} issues.")

    if failed_count > 0:
        sys.exit(1)


if __name__ == "__main__":
    main()
