#!/usr/bin/env bash
# The A/B report of one platform of the PGO workflow (#682): each optimized
# variant against the plain build, by benchmark-compare.py, and the build
# times with what PGO (and BOLT) add. Writes pgo-run/results/report.md and
# adds it to the job summary.
#
# Usage: pgo-report.sh <platform title>   (from the workspace root, after
#        pgo-unpack.sh and pgo.py measure)
set -euo pipefail

title=${1:?usage: pgo-report.sh <platform title>}
python=$(command -v python3 || command -v python)
results=pgo-run/results
report="$results/report.md"
mkdir -p "$results"

label() {
    case "$1" in
        plain) echo "without PGO (LOGSQUIRL_PGO=OFF)" ;;
        pgo) echo "with PGO (LOGSQUIRL_PGO=USE)" ;;
        bolt) echo "with PGO and BOLT (logsquirl and logsquirl_grep only)" ;;
    esac
}

{
    echo "## $title"
    echo
    echo "Commit ${GITHUB_SHA:0:12}, run $GITHUB_SERVER_URL/$GITHUB_REPOSITORY/actions/runs/$GITHUB_RUN_ID."
    echo
} > "$report"

for variant in pgo bolt; do
    [ -d "pgo-run/$variant" ] || continue
    if [ -d "$results/plain" ] && [ -d "$results/$variant" ]; then
        "$python" .github/scripts/benchmark-compare.py \
            --before "$results/plain" --after "$results/$variant" \
            --before-label "$(label plain)" --after-label "$(label "$variant")" \
            --markdown "$results/comparison-$variant.md" --json "$results/comparison-$variant.json"
        {
            echo "### $(label "$variant") against $(label plain)"
            echo
            cat "$results/comparison-$variant.md"
            echo
        } >> "$report"
    fi
    "$python" .github/scripts/pgo.py --timings "pgo-run/$variant/timings.json" times \
        --also pgo-run/plain/timings.json --title "$title, $variant" --markdown "$results/times-$variant.md" > /dev/null
    cat "$results/times-$variant.md" >> "$report"
    echo >> "$report"
done

cat "$report" >> "$GITHUB_STEP_SUMMARY"
