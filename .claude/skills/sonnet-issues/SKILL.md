---
name: sonnet-issues
description: "Manually analyze and prioritize open GitHub issues for the sonnet engine project. Call this skill to rank issues by feasibility and user impact, identify which issues fit with the current roadmap, tag issues that don't align well (architectural blockers, future milestones, out-of-scope), and suggest quick wins. The skill reads AGENTS.md, CLAUDE.md, and recent commits to understand engine status, then evaluates each open issue. Proposes improvements to issue text and drafts GitHub comments explaining why issues don't fit. Only invoke manually when you want to review and prioritize the issue backlog."
compatibility: "Requires GitHub CLI (gh), access to the current sonnet repository"
---

# sonnet-issues: Prioritize GitHub Issues by Roadmap Fit & Impact

## Overview

This skill helps you stay focused by answering: **Which open issues should we prioritize right now?** It analyzes the gap between issues and the current engine status, tags misaligned ones, and surfaces quick wins. Output integrates with your GitHub Project for tracking and execution.

**Security:** This skill does not store or leak sensitive data. Project identifiers are passed as parameters only when updating (not hardcoded). Field IDs are fetched dynamically at runtime.

Use this skill **manually** whenever you want to:
- Review the backlog and decide what to tackle next
- Understand why certain issues don't fit the current roadmap
- Find low-effort, high-impact work (UX improvements, docs, minor features)
- Draft GitHub comments explaining roadmap alignment to issue authors
- Generate a prioritized list to populate or update the project board

## GitHub Project Integration

The output of this skill is designed to automatically update your [GitHub Project board](https://github.com/users/Pacheco95/projects/1). The skill includes:

**Custom Fields** (already configured in your project):
1. **Priority Tier** (Single select)
   - Options: Quick Win, Medium Fit, Deferred

2. **Effort (hours)** (Number)
   - Specific hour estimates for each issue

3. **Target Release** (Single select)
   - Options at the time of writing: M9 (Mobile/Android), M10 (iOS), Post-1.0, M11+. **These go stale.** Read the live options before proposing anything:
     `gh project field-list 1 --owner Pacheco95 --format json`
   - Never target a milestone the roadmap marks as done. If the only remaining options are for finished milestones plus Post-1.0 and M11+, use those, and tell the user which options are stale and that a new one (for example "1.0.0" for work before the release tag) would let pre-release polish be tagged properly.

4. **Status** (Built-in)
   - Options: Todo, In Progress, Done

**Automated Workflow:**
1. Run the skill → get prioritized report + update summary
2. Review the summary table showing proposed changes
3. Approve: "Ready to update the GitHub Project?" (yes/no)
4. If yes: Skill automatically sets all fields via GitHub API
5. If no: Manual copy-paste option still available

**Benefits:**
- No manual data entry
- All issues updated consistently in seconds
- Reduces transcription errors
- Project always stays in sync with skill analysis

## Workflow

### Step 1: Understand Engine Status

Read the project context:
1. `docs/roadmap.md` — the source of truth for which milestones are done, in progress or next. `AGENTS.md`'s summary paragraph lags behind it.
2. The `project(sonnet VERSION ...)` line in the top-level `CMakeLists.txt` — the current version, to cross-check against the roadmap and the latest release tag
3. `AGENTS.md` and `CLAUDE.md` — project-specific guidance and priorities
4. Recent commit messages (last 20-30) — what was just shipped
5. `docs/decisions/` — ADRs that set architectural direction

Extract:
- Current milestone in progress, and every milestone already done (the roadmap decides, not your memory of an earlier run)
- Recently shipped features and their version
- Known architectural decisions that enable or constrain new work
- Open deferred items or known gaps

### Step 2: Fetch & Analyze Open Issues

Use `gh issue list --state open --limit 100` to fetch all open issues. For each issue:

**Assess Fit:**
- Does it align with the current or next planned milestone? ✓ fits
- Does it depend on work not yet done? ⚠️ blocked by X
- Is it architectural mismatch (e.g., requires redesign)? ✗ out-of-scope / architectural-mismatch
- Is it for a future platform (e.g., iOS when mobile is not ready)? ✗ future-milestone
- Is it docs, polish, or a known gap? ✓ quick-win or ⚠️ deferred-item

**Score by Impact vs Effort (in hours):**
- **Quick Win** (high UX impact, 1–6 hours): fix bugs, docs improvements, small feature polish, quality-of-life features
- **Medium** (moderate impact, 6–20 hours): features that fit the current milestone, modest scope
- **Heavy** (high impact, 20+ hours or architectural complexity): features that need a redesign or multi-milestone work, or require careful testing

The board has only three tiers, so map them like this:
- Quick Win → **Quick Win**, Medium → **Medium Fit**.
- Heavy → **Deferred**, whether it is blocked or merely large. Say which in the report: "Deferred (blocked)" or "Deferred (large but fits the roadmap)", and recommend splitting the large ones into steps that each fit a tier.
- The hour ranges decide the tier, not the wording. A 6–8 hour item is Medium Fit, not a Quick Win.

**Estimate from the code, not from the issue text or an earlier number.** Before giving hours, open the modules the issue touches and note what already exists and what does not. Typical findings that move an estimate:
- Machinery that already exists (a pause in another entry point, a per-user preferences file, a helper the change can reuse) lowers it.
- A premise in the issue that does not hold in the code (the suspected cause is not where the issue says, the feature is used in fewer or more places than it says) changes the scope, and sometimes the tier.
- Work that cannot be verified on this machine (a Mac, a phone) widens the range.
- Behaviour the issue does not mention but the design forces (for example, everything moves during play, so a raw diff is noise) adds work.

Put one line of evidence per issue in the report ("`Preferences` already stores per-user JSON, so no new config directory"). If a range comes from a rule of thumb rather than from reading code, say so.

When the user asks to re-evaluate, redo this reading for every issue. Never re-derive hours from the previous range, and never treat recomputing a stored value (a midpoint, a unit conversion) as a re-evaluation. Say plainly which of the two was done.

Tier follows the hours and the impact together: Quick Win needs a rounded midpoint of 6 hours or less and clear UX impact; more hours or only moderate impact is Medium Fit; blocked or heavy work is Deferred.

For each issue, estimate hours including:
- Design/spec time (if needed)
- Implementation time
- Testing and validation
- Code review and iteration cycles

Be realistic: a "small" feature that spans multiple systems or has platform-specific variants costs more than it looks.

### Step 3: Produce a Prioritized Report & Project Update Summary

Create a markdown report structured like this, followed by a summary table for project updates:

```markdown
# Issue Prioritization Report

**Generated**: [date]
**Engine Status**: [current milestone + version]
**Issues Analyzed**: N

## 🎯 Priority 1: Quick Wins (Do These First)
High UX impact, low code change. Ready to ship.

### #42 | [Title]
- **Impact**: [why this matters to users]
- **Effort**: [X hours] – [what changes: design, implementation, testing]
- **Blockers**: none
- [optional: "Consider renaming this to..." if text can improve]

---

## 📋 Priority 2: Medium Fit (Align with Milestone)
These fit the current/next milestone and should be tackled in order.

### #15 | [Title]
- **Impact**: [description]
- **Effort**: [X hours] – [scope and breakdown]
- **Milestone**: M9 (mobile) — fits because [reason]
- **Blocker**: [none or what's required first]

---

## ⚠️ Priority 3: Architectural/Deferred
Issues that don't fit now but are worth keeping.

### #88 | [Title]
- **Reason**: Blocked by M10 (animation system)
- **Impact**: [what it enables]
- **Effort**: [X hours] – (estimated if unblocked)
- **Suggested Comment**: "Thanks for the issue! This depends on work scheduled for M10..."

### #22 | [Title]
- **Reason**: Architectural mismatch — requires ECS redesign
- **Impact**: [value if we decide to do it]
- **Effort**: [X+ hours] – (high because architectural redesign needed)
- **Suggested Comment**: "This is a great idea, but it would require rearchitecting..."

---

## 💡 Improvements & Comments

List any issues where you recommend:
- Rewording the title or description for clarity
- Adding context (e.g., "relates to #XYZ")
- Drafting a comment explaining roadmap alignment

Include the issue number, suggested change, and the comment text to post.
```

### Step 4: Apply to Project (Automated After Approval)

After generating the prioritized report:

1. **Generate summary table** with proposed changes:
   ```
   | Issue # | Title | Priority Tier | Effort (hrs) | Target Release |
   |---------|-------|---------------|--------------|----------------|
   | #68 | Instantiate objects as children | Quick Win | 1–2 | Post-1.0 |
   | #59 | MoltenVK R32Uint warning | Quick Win | 2–4 | Post-1.0 |
   | #64 | Pause scene | Medium Fit | 6–8 | Post-1.0 |
   | ... | ... | ... | ... | ... |
   ```

2. **Ask for approval:**
   ```
   Ready to update the GitHub Project with these changes?
   - Update 10 issues
   - Set Priority Tier, Effort (hours), and Target Release fields
   (yes/no)
   ```

3. **If approved: Execute automated update**
   - Save summary table as JSON
   - Run from the repository root: `python3 .claude/skills/sonnet-issues/scripts/update_project.py --issues summary.json --project-owner USERNAME --project-number 1 --repo REPO`
   - `--repo` is the repository name only (`sonnet`), not `owner/repo`
   - Write `summary.json` in the scratchpad directory, not the repository
   - The Effort field stores the **midpoint** of a range, rounded (1–2 → 2, 6–8 → 7); a single number is stored as is
   - Script dynamically fetches project configuration (no hardcoded IDs)
   - Updates each issue's fields via GitHub GraphQL API
   - Report results: "✓ Updated 10/10 issues in 8 seconds"

4. **If declined: Show report only**
   - User can copy values manually from the summary table
   - Project remains unchanged

5. **Suggested changes are never applied on their own.** The report proposes title rewrites, issue splits, cross-links and comments; the project update above does none of them. Ask before doing each kind, and say plainly that they have not been done if the user asks.

### Step 5: Execute Approved Splits, Retitles and Links

When the user approves the suggestions from the "Improvements & Comments" section:

- **Edit issues with `gh api`**, not `gh issue edit` (it is broken here as `gh pr edit` is):
  `gh api -X PATCH repos/OWNER/REPO/issues/N -f title="..." -f body="..."`
- **Split an issue:** create the new issue first (`gh issue create`, keep the `enhancement`/`bug` label), then rewrite the original's title and body so each covers its part and points at the other ("Split from #N"). Do not leave the same scope in both.
- **Hard dependencies** use GitHub's native "blocked by", which takes the blocker's numeric `id`, not its number:
  ```
  gh api -X POST repos/OWNER/REPO/issues/BLOCKED/dependencies/blocked_by -F issue_id=$(gh api repos/OWNER/REPO/issues/BLOCKER -q .id)
  ```
  Read them back with `GET .../dependencies/blocked_by`. Only use it for real blockers.
- **Soft relations** ("related", "do first") have no native type: post a comment on both issues.
- **Add each new issue to the board** with `gh project item-add 1 --owner OWNER --url <issue url>`, then run `update_project.py` again for it and for any split parent whose estimate changed.
- Finish by listing the open issues and reading back the dependencies you set.

**Implementation:**
- Script uses field IDs and option IDs pre-configured from your project
- Queries project items via GitHub API
- Updates all fields in batch with 0.5s delays (respects rate limits)
- Handles errors gracefully with fallback to manual update option

## Tips for Accurate Prioritization

- **Roadmap vs. Nice-to-Have**: Read AGENTS.md carefully. Just because an issue is popular doesn't mean it fits the current plan. Conversely, issues that enable future work (like infrastructure improvements) may be worth doing early even if not immediately visible.

- **Estimates Are Evidence, Not Arithmetic**: an estimate that has not come from reading the affected code is a guess. Re-check it against the code when the user questions it.

- **Quick Wins Don't Stay Quick**: If an issue seems simple but touches multiple systems, it's not a quick win. Be honest about scope.

- **Blockers Are Real**: If an issue depends on another milestone's work, mark it clearly. Don't say "could maybe do this" if the foundation isn't there.

- **User Context Matters**: A one-line bug report that blocks 50 users is higher priority than a polished feature request from one person.

- **Improve Issue Text**: If an issue is vague ("things are slow") vs. specific ("FPS drops from 120 to 30 when spawning 10k entities in mobile"), suggest improvements. Better issues lead to faster fixes.

## Quick Reference: Automated Project Updates

Your [Sonnet Issue Prioritization project](https://github.com/users/Pacheco95/projects/1) is configured for automatic updates.

**How it works:**
1. Skill analyzes all open issues and generates report
2. Creates summary table showing:
   - Issue number
   - Priority Tier (Quick Win / Medium Fit / Deferred)
   - Effort Hours (specific range)
   - Target Release (M9 / M10 / Post-1.0 / M11+)
3. Asks for approval: "Ready to update the GitHub Project?"
4. If approved: Uses GitHub API to set all fields in batch
5. Reports success: "Updated 10 issues in 5 seconds"

**View options in project:**
- **Table**: See all fields side-by-side; best for reviewing updates
- **Kanban**: Drag issues through Todo → In Progress → Done
- **Filter**: "Priority Tier is Quick Win" to see high-priority items

**Manual override:**
- If API update fails, the skill provides the summary table for manual copy-paste
- You can edit fields directly in the project Table view anytime

**Workflow:** Run `/sonnet-issues` → Review report → Approve updates → Project automatically synced → Use board for sprint tracking

## Using the Output with GitHub Project

After running the skill, you'll have a report with each issue assigned to:
- **Priority Tier** (Quick Win / Medium Fit / Deferred)
- **Effort Hours** (specific range)
- **Milestone** (which release it fits)
- **Reasoning** (why it belongs in that tier)

To update the project:
1. Open [the GitHub Project](https://github.com/users/Pacheco95/projects/1)
2. Switch to **Table** view
3. For each issue in the skill report:
   - Set **Priority Tier** to the tier from the report
   - Set **Effort (hours)** to the hour estimate
   - Set **Milestone** if specified
4. Filter by Priority Tier to see **Quick Wins** at the top
5. Use the project's kanban view to track progress as work moves from Backlog → Ready → In Progress → Done

## Example

**Issue #42**: "Add a gizmo for rotating objects in the editor"

```
Priority Tier: Medium Fit
Milestone: M9 (mobile focus, but editor improvements fit any milestone)
Impact: Faster world-building, better UX
Effort Hours: 12–16 (design handles, test all axes, iterate with feedback)
Note: Gizmo framework exists from M1; ready to design & implement; pair with similar gizmo work
```

**Issue #88**: "Support iOS deployment"

```
Priority Tier: Deferred
Milestone: M10+ (iOS is not planned yet; Android is M9)
Impact: Critical for iOS users (future market)
Effort Hours: 80–120+ (platform integration, Vulkan driver updates, testing, unknowns)
Status: Post a comment: "iOS support is planned for M10. M9 focuses on Android stability..."
```
