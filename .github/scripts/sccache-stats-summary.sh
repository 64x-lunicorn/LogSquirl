#!/usr/bin/env bash
# Reads `sccache --show-stats` output on stdin, echoes it to the log and appends
# the hit/miss lines to the job summary, so the hit rate of every build can be
# compared at a glance instead of digging through each job's log (#219).
#
# Usage: sccache --show-stats | sccache-stats-summary.sh <build name>
set -euo pipefail

name="${1:?usage: sccache-stats-summary.sh <build name>}"
stats=$(cat)
printf '%s\n' "$stats"

if [ -z "${GITHUB_STEP_SUMMARY:-}" ]; then
    exit 0
fi

# The label/value lines only; the per-language breakdowns and the cache
# location block would bury the one number that matters.
compact=$(printf '%s\n' "$stats" |
    grep -E '^(Compile requests|Cache hits|Cache misses|Non-cacheable calls)' || true)
if [ -z "$compact" ]; then
    compact="(no sccache statistics available)"
fi

fence='```'
printf '### sccache: %s\n\n%s\n%s\n%s\n\n' "$name" "$fence" "$compact" "$fence" >>"$GITHUB_STEP_SUMMARY"
