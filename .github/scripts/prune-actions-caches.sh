#!/usr/bin/env bash
# Deletes Actions cache entries no build can use any more, so the repository
# stays under GitHub's 10 GB cache limit instead of evicting entries that
# builds still need (#219). An entry is stale when
#
#   closed-pr       it belongs to a pull request that is no longer open;
#   deleted-branch  it belongs to a branch that no longer exists;
#   superseded      a newer entry with the same key prefix exists on the same
#                   ref (keys ending in a run id/attempt, a commit sha or a
#                   content hash, e.g. sccache-Linux-noble-<run>-<attempt>,
#                   codeql-trap-...-<sha>, cpm-Linux-noble-<hash>). Caches are
#                   immutable, so these keys are the only way to refresh an
#                   entry, and only the newest one is ever restored;
#   stale-object    it is an sccache GHA-backend object (key sccache/...) not
#                   read or written within the grace period before the newest
#                   object activity on its ref, i.e. no recent build used it.
#
# Entries on tags and everything else are left to GitHub's own 7-day eviction.
#
# Usage: prune-actions-caches.sh [--repo owner/name] [--dry-run]
#                                [--object-grace-hours N]
# Needs gh (authenticated; deleting needs actions: write) and jq.
set -euo pipefail

repo="${GITHUB_REPOSITORY:-}"
dry_run=false
object_grace_hours=48

usage() {
    sed -n '2,/^set -euo/p' "$0" | sed '$d; s/^# \{0,1\}//'
}

while [ $# -gt 0 ]; do
    case "$1" in
    --repo)
        repo="${2:?--repo needs a value}"
        shift 2
        ;;
    --dry-run)
        dry_run=true
        shift
        ;;
    --object-grace-hours)
        object_grace_hours="${2:?--object-grace-hours needs a value}"
        shift 2
        ;;
    -h | --help)
        usage
        exit 0
        ;;
    *)
        echo "unknown argument: $1" >&2
        usage >&2
        exit 2
        ;;
    esac
done

if [ -z "$repo" ]; then
    echo "no repository: pass --repo owner/name or set GITHUB_REPOSITORY" >&2
    exit 2
fi
case "$object_grace_hours" in
'' | *[!0-9]*)
    echo "--object-grace-hours must be a whole number" >&2
    exit 2
    ;;
esac

caches=$(gh api --paginate "repos/$repo/actions/caches?per_page=100" --jq '.actions_caches[]' | jq -s .)
open_prs=$(gh api --paginate "repos/$repo/pulls?state=open&per_page=100" --jq '.[].number' | jq -s .)
branches=$(gh api --paginate "repos/$repo/branches?per_page=100" --jq '.[].name' | jq -R . | jq -s .)

# One line per stale entry, largest first, so a run that stops early (API rate
# limit) has already freed the most space.
stale=$(jq -c \
    --argjson open "$open_prs" \
    --argjson branches "$branches" \
    --argjson grace "$((object_grace_hours * 3600))" '
    def epoch: sub("\\.[0-9]+Z$"; "Z") | fromdateiso8601;
    def prefix: sub("-[0-9]+-[0-9]+$"; "") | sub("-([0-9a-f]{40}|[0-9a-f]{64})$"; "");
    def gone:
        if (.ref | startswith("refs/pull/")) then
            (.ref | capture("^refs/pull/(?<n>[0-9]+)/").n | tonumber) as $n
            | if ($open | any(. == $n)) then null else "closed-pr" end
        elif (.ref | startswith("refs/heads/")) then
            (.ref | ltrimstr("refs/heads/")) as $b
            | if ($branches | any(. == $b)) then null else "deleted-branch" end
        else null end;

    (map(. + {reason: gone})) as $all
    | ($all | map(select(.reason != null))) as $gone
    | ($all | map(select(.reason == null))) as $live
    | ($live
        | map(select(.key | startswith("sccache/") | not))
        | group_by([.ref, (.key | prefix)])
        | map(sort_by(.created_at) | .[:-1][] | .reason = "superseded")) as $superseded
    | ($live
        | map(select(.key | startswith("sccache/")))
        | group_by(.ref)
        | map((map(.last_accessed_at | epoch) | max) as $newest
              | .[] | select((.last_accessed_at | epoch) < $newest - $grace)
              | .reason = "stale-object")) as $objects
    | ($gone + $superseded + $objects)
    | sort_by(-.size_in_bytes)[]
    | {id, key, ref, size_in_bytes, last_accessed_at, reason}
    ' <<<"$caches")

gib() { LC_ALL=C awk -v b="$1" 'BEGIN { printf "%.2f", b / 1073741824 }'; }

total_count=$(jq 'length' <<<"$caches")
total_bytes=$(jq '[.[].size_in_bytes] | add // 0' <<<"$caches")
echo "Repository $repo: $total_count cache entries, $(gib "$total_bytes") GiB"
mode_note=""
if $dry_run; then
    mode_note=" (dry run)"
    echo "Dry run: nothing is deleted."
fi

deleted_ids=()
failed=0
rate_limited=false
while IFS=$'\t' read -r id reason mib ref key; do
    [ -n "$id" ] || continue
    printf '%-14s %8s MiB  %-22s %s\n' "$reason" "$mib" "$ref" "$key"
    if $dry_run; then
        deleted_ids+=("$id")
        continue
    fi
    if output=$(gh api -X DELETE "repos/$repo/actions/caches/$id" 2>&1); then
        deleted_ids+=("$id")
    elif grep -q "HTTP 404" <<<"$output"; then
        # Evicted or deleted by someone else in the meantime: already gone.
        deleted_ids+=("$id")
    elif grep -qiE "rate limit|HTTP 429" <<<"$output"; then
        # The GITHUB_TOKEN allows 1,000 API requests per hour; the next run
        # picks up where this one stopped.
        echo "::warning::API rate limit reached; the remaining entries are left for the next run"
        rate_limited=true
        break
    else
        echo "::warning::could not delete cache $id: $output"
        failed=$((failed + 1))
    fi
done < <(jq -r '[.id, .reason, (.size_in_bytes / 1048576 | floor), .ref, .key] | @tsv' <<<"$stale")

ids_json=$(printf '%s\n' "${deleted_ids[@]+"${deleted_ids[@]}"}" | jq -R 'select(length > 0) | tonumber' | jq -s .)
report=$(jq -r --argjson ids "$ids_json" --arg dry "$dry_run" '
    def gib: . / 1073741824 * 100 | round / 100;
    map(select(.id as $i | $ids | any(. == $i))) as $done
    | (if $dry == "true" then "would be deleted" else "deleted" end) as $verb
    | "| Reason | Entries | GiB |",
      "| --- | ---: | ---: |",
      ($done | group_by(.reason)[] | "| \(.[0].reason) | \(length) | \(map(.size_in_bytes) | add | gib) |"),
      "| **total \($verb)** | **\($done | length)** | **\($done | map(.size_in_bytes) | add // 0 | gib)** |"
    ' <<<"$(jq -s . <<<"$stale")")

freed_bytes=$(jq -s --argjson ids "$ids_json" \
    'map(select(.id as $i | $ids | any(. == $i)) | .size_in_bytes) | add // 0' <<<"$stale")
remaining=$((total_bytes - freed_bytes))

echo
echo "$report"
echo "Cache usage: $(gib "$total_bytes") GiB before, $(gib "$remaining") GiB after$mode_note"

if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
    {
        echo "## Actions cache cleanup$mode_note"
        echo
        echo "$report"
        echo
        echo "Cache usage: $(gib "$total_bytes") GiB in $total_count entries before, $(gib "$remaining") GiB after."
        if $rate_limited; then
            echo
            echo "Stopped at the API rate limit; the next run continues."
        fi
    } >>"$GITHUB_STEP_SUMMARY"
fi

if [ "$failed" -gt 0 ]; then
    echo "::error::$failed cache entries could not be deleted"
    exit 1
fi
