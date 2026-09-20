"""Validate Conventional Commits subjects (docs/conventions.md, "Commits").

Usage: check_commit_msg.py <file>   (git commit-msg hook)
       check_commit_msg.py --stdin  (one subject per line, e.g. from git log --format=%s)
"""
import re
import sys

TYPES = "feat|fix|perf|refactor|docs|test|build|ci|style|chore|revert"
SCOPES = (
    "core|platform|rhi|renderer|assets|world|physics|scripting|audio|runtime|ui|editor|player|cook|samples|cmake|deps|docs"
)
SUBJECT = re.compile(rf"^({TYPES})(\(({SCOPES})\))?!?: [a-z0-9].*[^.]$")
MERGE_OR_REVERT = re.compile(r"^(Merge |Revert \")")


def check(subject: str) -> str | None:
    if MERGE_OR_REVERT.match(subject):
        return None
    if len(subject) > 72:
        return "subject longer than 72 characters"
    if not SUBJECT.match(subject):
        return "expected '<type>(<scope>): <imperative lowercase description>' with a known type and scope"
    return None


def main() -> int:
    if len(sys.argv) == 2 and sys.argv[1] == "--stdin":
        subjects = [line.rstrip("\n") for line in sys.stdin if line.strip()]
    elif len(sys.argv) == 2:
        with open(sys.argv[1], encoding="utf-8") as f:
            lines = [line.rstrip("\n") for line in f if not line.startswith("#")]
        subjects = [next((line for line in lines if line.strip()), "")]
    else:
        print(__doc__)
        return 2

    failed = False
    for subject in subjects:
        problem = check(subject)
        if problem:
            failed = True
            print(f"bad commit subject: {subject!r}: {problem}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
