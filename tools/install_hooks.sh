#!/bin/sh
# Installs the local git hooks: commit-msg validates Conventional Commits, pre-commit enforces the
# one-way module dependency rule.
set -e
root=$(git rev-parse --show-toplevel)
hooks=$(git rev-parse --git-path hooks)
cat > "$hooks/commit-msg" <<'HOOK'
#!/bin/sh
exec python3 "$(git rev-parse --show-toplevel)/tools/check_commit_msg.py" "$1"
HOOK
cat > "$hooks/pre-commit" <<'HOOK'
#!/bin/sh
# Only runs when the commit touches modules, apps or CMake files.
if git diff --cached --name-only | grep -qE '^(modules|apps)/|(^|/)CMakeLists\.txt$|\.cmake$'; then
  exec python3 "$(git rev-parse --show-toplevel)/tools/check_dependencies.py"
fi
HOOK
chmod +x "$hooks/commit-msg" "$hooks/pre-commit"
echo "installed $hooks/commit-msg and $hooks/pre-commit"
