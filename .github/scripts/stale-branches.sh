#!/usr/bin/env bash
# Which branches and worktrees are left over from merged pull requests (#773).
#
# A squash merge leaves no trace of the branch in master, and master moves on,
# so comparing content proves nothing. A branch is "merged" when its pull
# request was merged and the branch tip is the head that was merged, or an
# ancestor of it: nothing on the branch is missing from the merge. Anything
# else is kept and says why.
#
# Usage:
#   .github/scripts/stale-branches.sh
#
# Reports every worktree besides this checkout (the agents' under
# .claude/worktrees/ among them), every local branch and every branch on
# origin, then prints the commands that remove the merged
# ones. It deletes nothing itself. master, release/* and the data branches on
# origin (perf-data, assets/*) are never listed. Needs `gh` authenticated.
#
# Written for the bash 3.2 of macOS: no associative arrays, and an empty
# array is expanded as ${a[@]+"${a[@]}"} under set -u.
set -euo pipefail

cd "$(git rev-parse --show-toplevel)"
git fetch -q --prune origin

keep() { # branch -> 0 when the branch is never a candidate
  case "$1" in
    master | release/* | perf-data | assets/*) return 0 ;;
    *) return 1 ;;
  esac
}

# Prints "merged #N", "open #N", "closed #N", "unmerged commits #N", "in master"
# (no pull request, but the tip is in master) or "no PR" for a branch and its tip.
classify() {
  local branch=$1 tip=$2 pr number state head
  pr=$(gh pr list --head "$branch" --state all --limit 1 --json number,state,headRefOid \
    --jq '.[0] | select(.) | "\(.number) \(.state) \(.headRefOid)"')
  if [ -z "$pr" ]; then
    if git merge-base --is-ancestor "$tip" origin/master; then echo "in master"; else echo "no PR"; fi
    return
  fi
  read -r number state head <<<"$pr"
  case "$state" in
    OPEN) echo "open #$number"; return ;;
    CLOSED) echo "closed #$number"; return ;;
  esac
  git cat-file -e "$head^{commit}" 2>/dev/null || git fetch -q origin "pull/$number/head"
  if [ "$tip" = "$head" ] || git merge-base --is-ancestor "$tip" "$head"; then
    echo "merged #$number"
  else
    echo "unmerged commits #$number"
  fi
}

remove_worktrees=()
delete_local=()
delete_remote=()

echo "== worktrees"
worktree_branches="" # "branch path" lines
while read -r path branch; do
  [ "$path" = "$PWD" ] && continue
  [ -n "$branch" ] && worktree_branches+="$branch $path"$'\n'
  dirty=$(git -C "$path" status --porcelain | wc -l | tr -d ' ')
  if [ -z "$branch" ]; then
    status="detached HEAD"
  else
    status=$(classify "$branch" "$(git rev-parse "$branch")")
  fi
  if [ "$dirty" != 0 ]; then
    status="$status, $dirty uncommitted changes"
  elif [[ "$status" == merged* || "$status" == "in master" ]]; then
    remove_worktrees+=("$path")
  fi
  printf '  %-60s %s\n' "${path#"$PWD"/}" "$status"
done < <(git worktree list --porcelain |
  awk '/^worktree /{p=substr($0,10)} /^branch /{b=substr($0,19)} /^$/{print p" "b; p=""; b=""} END{if(p)print p" "b}')

echo "== local branches"
while read -r branch; do
  keep "$branch" && continue
  status=$(classify "$branch" "$(git rev-parse "$branch")")
  wt=$(awk -v b="$branch" '$1 == b { print substr($0, length(b) + 2) }' <<<"$worktree_branches")
  if [[ "$status" == merged* || "$status" == "in master" ]] && [ "$branch" != "$(git branch --show-current)" ]; then
    # A worktree holding the branch must go first; one with changes keeps it.
    if [ -z "$wt" ] || [[ " ${remove_worktrees[*]+${remove_worktrees[*]}} " == *" $wt "* ]]; then
      delete_local+=("$branch")
    fi
  fi
  printf '  %-60s %s%s\n' "$branch" "$status" "${wt:+ (worktree)}"
done < <(git for-each-ref --format='%(refname:short)' refs/heads)

echo "== branches on origin"
while read -r branch; do
  keep "$branch" && continue
  status=$(classify "$branch" "$(git rev-parse "origin/$branch")")
  [[ "$status" == merged* ]] && delete_remote+=("$branch")
  printf '  %-60s %s\n' "$branch" "$status"
done < <(git for-each-ref --format='%(refname:lstrip=3)' refs/remotes/origin | grep -v '^HEAD$')

if [ ${#remove_worktrees[@]} -eq 0 ] && [ ${#delete_local[@]} -eq 0 ] && [ ${#delete_remote[@]} -eq 0 ]; then
  echo
  echo "Nothing to remove."
  exit 0
fi
echo
echo "Remove the merged ones with:"
for w in ${remove_worktrees[@]+"${remove_worktrees[@]}"}; do
  echo "  git worktree remove '$w'"
done
if [ ${#delete_local[@]} -gt 0 ]; then echo "  git branch -D ${delete_local[*]}"; fi
if [ ${#delete_remote[@]} -gt 0 ]; then echo "  git push origin --delete ${delete_remote[*]}"; fi
exit 0
