#!/usr/bin/env bash
# Runs the benchmarks of both sides of the Benchmarks workflow (#276) on this
# runner: every Catch2 benchmark binary either side has, before and then after,
# and then the e2e performance suite on each side's logsquirl and
# logsquirl_grep. Each side runs in the container it was built in.
#
# Writes <RUN_DIR>/results/<side>/catch2/<binary>.xml (Catch2 XML reporter) and
# <RUN_DIR>/results/<side>/e2e/benchmark_report.json, which
# benchmark-compare.py reads. A benchmark that fails is an error, but every
# other one still runs, so the comparison shows what could be measured.
#
# The e2e suite is the checked-out one (the after side's) for both sides, with
# --update-baseline: its baseline.json was recorded on another machine, so the
# comparison with it would fail for reasons unrelated to either side; the
# before side is the baseline here.
#
# Usage: run-benchmarks.sh   (from the workspace root)
# Environment: BEFORE_CONTAINER, AFTER_CONTAINER, RUN_DIR, BENCHMARK_SAMPLES,
# LOG_FILE_MB, E2E_RUNS, QT_QPA_PLATFORM and LOGSQUIRL_WORKSPACE.
set -euo pipefail

status=0

container_of() {
    if [ "$1" = before ]; then echo "$BEFORE_CONTAINER"; else echo "$AFTER_CONTAINER"; fi
}

in_container() {
    local side=$1
    shift
    docker run --rm \
        --env QT_QPA_PLATFORM="$QT_QPA_PLATFORM" \
        --env LOGSQUIRL_BENCHMARK_LOG_FILE_MB="$LOG_FILE_MB" \
        --env PYTHONDONTWRITEBYTECODE=1 \
        --env PIP_ROOT_USER_ACTION=ignore \
        --workdir /usr/local \
        -v "$LOGSQUIRL_WORKSPACE":/usr/local "$(container_of "$side")" "$@"
}

binaries=$(find "$RUN_DIR/before/bin" "$RUN_DIR/after/bin" -maxdepth 1 -type f -name '*_benchmark' -exec basename {} \; | sort -u)

for binary in $binaries; do
    for side in before after; do
        if [ ! -x "$RUN_DIR/$side/bin/$binary" ]; then
            echo "::notice::$binary does not exist on the $side side"
            continue
        fi
        echo "::group::$binary ($side)"
        if ! in_container "$side" "$RUN_DIR/$side/bin/$binary" \
                --reporter xml --out "$RUN_DIR/results/$side/catch2/$binary.xml" \
                --benchmark-samples "$BENCHMARK_SAMPLES"; then
            echo "::error::$binary failed on the $side side"
            status=1
        fi
        echo "::endgroup::"
    done
done

for side in before after; do
    echo "::group::e2e performance suite ($side)"
    # The report lands next to conftest.py; move it before the other side
    # overwrites it.
    # shellcheck disable=SC2016 # expanded by the shell in the container
    if ! in_container "$side" /bin/bash -c '
            set -euo pipefail
            python3 -m pip install --quiet --break-system-packages "pytest>=7"
            cd tests/e2e
            rm -f benchmark_report.json
            python3 -m pytest -m performance -p no:cacheprovider \
                --binary-dir="/usr/local/$1" --bench-runs "$2" \
                --bench-report json --update-baseline
            mv benchmark_report.json "/usr/local/$3/benchmark_report.json"
        ' e2e "$RUN_DIR/$side/bin" "$E2E_RUNS" "$RUN_DIR/results/$side/e2e"; then
        echo "::error::The e2e performance suite failed on the $side side"
        status=1
    fi
    echo "::endgroup::"
done

exit $status
