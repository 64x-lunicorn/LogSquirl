#!/usr/bin/env bash
# Fails when a workflow or composite action uses a third-party action that is
# not pinned to a full commit SHA with its release version as a comment (#202):
#
#   uses: owner/repo[/path]@<40-hex commit sha> # vX.Y.Z
#
# A tag can be moved to other code; a commit SHA cannot. The version comment is
# what Dependabot reads and rewrites when it bumps the pin. Local actions
# (./.github/actions/...) are exempt; docker:// images must carry a digest.
#
# Usage: .github/scripts/check-action-pins.sh   (from the repository root)
set -euo pipefail

pinned='^[[:space:]]*(-[[:space:]]+)?uses:[[:space:]]+[A-Za-z0-9_.-]+/[A-Za-z0-9_./-]+@[0-9a-f]{40}[[:space:]]+#[[:space:]]+v[0-9]+(\.[0-9]+)*[[:space:]]*$'
exempt='^[[:space:]]*(-[[:space:]]+)?uses:[[:space:]]+(\./|docker://[^[:space:]]+@sha256:[0-9a-f]{64}([[:space:]]|$))'

status=0
while IFS= read -r hit; do
    file=${hit%%:*}
    rest=${hit#*:}
    line=${rest%%:*}
    text=${rest#*:}
    if ! grep -Eq "$pinned" <<<"$text" && ! grep -Eq "$exempt" <<<"$text"; then
        echo "::error file=$file,line=$line::action not pinned to a commit SHA with a '# vX.Y.Z' comment:${text}"
        status=1
    fi
done < <(grep -nE '^[[:space:]]*(-[[:space:]]+)?uses:' \
    .github/workflows/*.y*ml .github/actions/*/action.y*ml)

if [ "$status" -eq 0 ]; then
    echo "Every action is pinned to a commit SHA."
fi
exit $status
