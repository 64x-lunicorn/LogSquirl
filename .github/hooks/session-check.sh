#!/usr/bin/env bash
# SessionStart hook: warn once per session about missing setup, missing CONTEXT.md,
# and a review that has fallen behind. Silent when the repo is in good shape.
set -uo pipefail

REVIEW_THRESHOLD=20

root="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
cd "$root" || exit 0

msg=""
add() {
  [ -n "$msg" ] && msg="$msg\\n"
  msg="$msg- $1"
}

if [ ! -f .github/unisquirl.md ]; then
  add "This repo has no .github/unisquirl.md, so it was never set up. Before running any skill that reads or writes issues, tell the user to run /setup."
fi

if [ ! -f CONTEXT.md ] && [ ! -f CONTEXT-MAP.md ]; then
  add "This repo has no CONTEXT.md, so there is no agreed vocabulary to write in. Tell the user to run /setup, which creates it."
fi

if [ -f .github/.unisquirl-review ]; then
  last="$(head -n 1 .github/.unisquirl-review | tr -d '[:space:]')"
  if [ -n "$last" ] && git cat-file -e "${last}^{commit}" 2>/dev/null; then
    behind="$(git rev-list --count "${last}..HEAD" 2>/dev/null || echo 0)"
    if [ "$behind" -ge "$REVIEW_THRESHOLD" ]; then
      add "$behind commits since the last review. Suggest /code-review (against $last), or /improve-codebase-architecture if the user wants the bigger picture."
    fi
  fi
fi

[ -z "$msg" ] && exit 0

printf '{"systemMessage":"Unisquirl checks:\\n%s"}\n' "$msg"
exit 0
