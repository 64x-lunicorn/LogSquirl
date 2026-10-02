#!/usr/bin/env bash
# Runs the e2e performance suite of the checked-out commit on one set of
# binaries, in the build container, for the Performance workflow (#441, #685):
# this commit's, and the reference release's on the same runner.
#
# Writes <result dir>/benchmark_report.json and <result dir>/junit.xml. The
# report is kept even when a benchmark failed, so a reference that cannot run
# every scenario still has the others measured; the exit status is the
# suite's.
#
# Usage: perf-run-suite.sh BIN_DIR RESULT_DIR   (from the workspace root; both
#        relative to it)
# Environment: E2E_RUNS, QT_QPA_PLATFORM, LOGSQUIRL_WORKSPACE and
# LOGSQUIRL_CONTAINER as set by the composite actions.
set -euo pipefail

bin_dir=${1:?usage: perf-run-suite.sh BIN_DIR RESULT_DIR}
result_dir=${2:?usage: perf-run-suite.sh BIN_DIR RESULT_DIR}
mkdir -p "$result_dir"

# shellcheck disable=SC2016 # expanded by the shell in the container
docker run --rm \
    --env QT_QPA_PLATFORM="$QT_QPA_PLATFORM" \
    --env PYTHONDONTWRITEBYTECODE=1 \
    --env PIP_ROOT_USER_ACTION=ignore \
    --workdir /usr/local \
    -v "$LOGSQUIRL_WORKSPACE":/usr/local "$LOGSQUIRL_CONTAINER" /bin/bash -c '
        set -euo pipefail
        python3 -m pip install --quiet --break-system-packages --require-hashes --only-binary :all: \
            -r .github/requirements/e2e.txt
        cd tests/e2e
        rm -f benchmark_report.json
        status=0
        python3 -m pytest -v -rs -m performance -p no:cacheprovider --require-binaries \
            --no-baseline-compare --binary-dir="/usr/local/$1" \
            --bench-runs "$2" --bench-report json --junitxml="/usr/local/$3/junit.xml" || status=$?
        if [ -f benchmark_report.json ]; then
            mv benchmark_report.json "/usr/local/$3/benchmark_report.json"
        fi
        exit "$status"
    ' e2e "$bin_dir" "$E2E_RUNS" "$result_dir"
