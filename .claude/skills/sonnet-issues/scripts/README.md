# sonnet-issues Skill Scripts

## update_project.py

Automatically updates GitHub Project fields for prioritized issues after skill approval.

### Security

✓ **No hardcoded sensitive data** — Project ID, field IDs, and option IDs are fetched dynamically  
✓ **Reusable across projects** — Use with any GitHub project by specifying owner/number/repo  
✓ **Minimal permissions** — Only reads project metadata and updates specific fields  
✓ **Rate-limited** — 0.5s delays between updates to respect GitHub API limits

### Usage

```bash
python3 .claude/skills/sonnet-issues/scripts/update_project.py \
  --issues summary.json \
  --project-owner USERNAME \
  --project-number 1 \
  --repo REPO_NAME
```

**Parameters:**
- `--issues` — Path to the prioritized issues JSON file (required)
- `--project-owner` — GitHub username that owns the project (required)
- `--project-number` — Project number (visible in URL: github.com/users/OWNER/projects/NUMBER)
- `--repo` — Repository name only, e.g. `sonnet`, not `owner/sonnet` (required)

### Input Format

The script expects a JSON file with this structure:

```json
[
  {
    "number": 68,
    "title": "Instantiate objects as children",
    "priority_tier": "Quick Win",
    "effort_hours": "1-2",
    "target_release": "M9 (Mobile/Android)"
  },
  {
    "number": 59,
    "title": "MoltenVK R32Uint warning",
    "priority_tier": "Quick Win",
    "effort_hours": "2-4",
    "target_release": "M9 (Mobile/Android)"
  }
]
```

### What It Does

1. **Dynamically fetches project configuration** (project ID, field IDs, option IDs) from GitHub
2. **Queries each issue's project item ID** via GitHub GraphQL API
3. **Updates three fields** for each issue:
   - `Priority Tier` (single select: Quick Win / Medium Fit / Deferred)
   - `Effort (hours)` (number: the rounded midpoint of a range, e.g., "1-2" → 2, "6-8" → 7)
   - `Target Release` (single select; the options come from the live project and go stale as milestones land)
4. **Batches updates** with 0.5s delays to respect GitHub API rate limits
5. **Reports progress** with ✓/✗ for each field update

### Output

```
Fetching project configuration...

Updating 10 issues in the GitHub Project...

[1/10] Updating #68... ✓ ✓ ✓
[2/10] Updating #59... ✓ ✓ ✓
[3/10] Updating #64... ✓ ✓ ✓
...
[10/10] Updating #56... ✓ ✓ ✓

Done! Updated 10/10 issues.
```

### Prerequisites

- GitHub CLI (`gh`) installed and authenticated
- `gh` must have project permissions: `gh auth refresh -s project,read:project`
- Python 3.6+

### Dynamic Configuration

The script automatically fetches and uses:
- Project ID from the GraphQL API
- Field IDs (`Priority Tier`, `Effort (hours)`, `Target Release`) via introspection
- Option IDs for single-select fields (e.g., "Quick Win" → actual ID)

**If project fields change**, the script adapts automatically—no code changes needed.

### Error Handling

- If a project item lookup fails, the issue is marked as failed and skipped
- If a field update fails, the script continues with other issues
- Exit code 1 if any updates fail; 0 if all succeed
- Detailed error messages are printed to stderr

### Manual Fallback

If the script fails or you prefer manual updates:

1. Open [Sonnet Issue Prioritization project](https://github.com/users/Pacheco95/projects/1)
2. Switch to **Table** view
3. For each issue in the skill's summary table:
   - Click the cell and enter the Priority Tier
   - Enter the Effort Hours (numeric)
   - Select the Target Release

Updates should complete in under 30 seconds for 10 issues.
