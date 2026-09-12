#!/bin/sh
# Installs the commit-msg hook that validates Conventional Commits locally.
set -e
root=$(git rev-parse --show-toplevel)
cat > "$root/.git/hooks/commit-msg" <<'HOOK'
#!/bin/sh
exec python3 "$(git rev-parse --show-toplevel)/tools/check_commit_msg.py" "$1"
HOOK
chmod +x "$root/.git/hooks/commit-msg"
echo "installed .git/hooks/commit-msg"
