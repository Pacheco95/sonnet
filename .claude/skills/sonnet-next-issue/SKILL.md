---
name: sonnet-next-issue
description: "Take the top-most Ready for dev card on the private 'Sonnet Issue Prioritization' GitHub project board, implement it, and carry it through the board and Git: move the card to In Progress, work on a feature branch, open a PR, and once the user has merged it in the GitHub UI move the card to Done and tidy the local checkout (checkout main, pull, delete the local feature branch). Use whenever the user says 'next issue', 'pick up the next issue', 'work on the top of the board', 'take the next card', 'do the next quick win', or asks to work through the sonnet backlog, even if they do not name the board."
compatibility: "Requires the GitHub CLI (gh) logged in as the repository owner with the project and read:project scopes, and a clean checkout of Pacheco95/sonnet"
disable-model-invocation: true
---

# sonnet-next-issue

Work the backlog one card at a time: the board decides what is next, the PR is the deliverable, and the local checkout ends where it started (clean `main`, no stale branch). The ranking is the owner's ([sonnet-issues](../sonnet-issues/SKILL.md) maintains it), so do not re-rank or skip cards on taste.

The repository is on GitHub (`Pacheco95/sonnet`), and GitHub is configured to delete the remote branch after a merge, so never delete a remote branch by hand.

## 1. Preflight

- `git status --short` must be empty and the branch should be `main`. If not, stop and tell the user; do not stash or discard their work to make room.
- `git fetch --prune && git pull --ff-only` so the branch starts from the current `main`.
- `python3 .claude/skills/sonnet-next-issue/scripts/board.py next` prints the top-most card whose Status is Ready for dev (board order is the priority order). Only Ready for dev cards are picked up for development: Backlog and Refining cards (still being clarified, possibly waiting for an ADR) are never taken, whatever their position. Read the issue itself with `gh issue view N --comments`: comments often carry the design decision or a soft dependency.
- If no card is Ready for dev, tell the user and stop.
- If `open_blockers` is not empty, or the issue is too vague to act on, show the user the card and ask; do not silently take the second card, because the order is theirs to change.
- A card already In Progress means earlier work may be half done. Mention it and ask before starting another.

## 2. Move the card to In Progress

`python3 .claude/skills/sonnet-next-issue/scripts/board.py status N "In Progress"`

Do this before the first edit, so the board shows the work is taken. Then create the branch from `main`, named after the change the way the existing ones are (`feat/…`, `fix/…`, `docs/…`).

## 3. Implement

Follow `AGENTS.md`: Conventional Commits with the module as scope (subject at most 72 characters, no `fixup!`; run `sh tools/install_hooks.sh` once per clone if the commit-msg hook is missing), a test with every bug fix, docs updated with the behaviour (`tools/check_docs.py`), and the build/test commands from `AGENTS.md` and `CLAUDE.local.md` (for example `VCPKG_ROOT=$HOME/vcpkg cmake --build --preset linux-debug`). Verify before opening the PR; say plainly what was and was not verified (a Mac or phone-only path cannot be checked here).

Commit attribution follows the session's reminder. Keep commits reviewable, one concern each.

## 4. Open the PR

Push and create the PR in two separate calls, not one chained command: a rejection in a chain does not undo the steps that already ran. The PR body says what changed and how it was verified, and contains `Closes #N` so the merge closes the issue. Then watch CI (`gh pr checks --watch`).

Never merge the PR yourself: the owner merges through the GitHub UI. If CI fails, fix it on the branch and push again. When CI is green, give the user the PR link and stop until they say it is merged; the card stays In Progress meanwhile.

## 5. After the merge

Start when the user says the PR is merged.

1. Confirm it really merged: `gh pr view --json state,mergeCommit` shows `MERGED`.
2. `python3 .claude/skills/sonnet-next-issue/scripts/board.py status N Done`, and check the issue is closed (`gh issue view N --json state`).
3. `git checkout main && git pull --ff-only && git fetch --prune`. The remote branch is already gone.
4. Delete the local feature branch you created: `git branch -d <branch>`. Only if that refuses because the PR was rebase-merged (the commits have new hashes) and step 1 showed `MERGED`, use `-D` for this one branch.
5. Confirm `git status --short` is empty and `HEAD` equals `origin/main`. Report the PR link and any deferred follow-ups (open a new issue for them rather than growing this PR).

## Branches you must not delete

Only delete the branch this run created. Branches made for reports or as a communication channel with other AI agents (`agents/*`, `issue-assets/*`, anything a device or Mac agent pushed or that a report points at) carry information that is not in `main`; leave them alone, locally and on the remote, and do not clean them up as "merged" or "stale". If tidying one seems warranted, ask the user and wait for an explicit yes. Deleting means `git branch -d/-D` and `git push --delete` alike.
