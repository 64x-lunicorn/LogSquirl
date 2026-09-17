#!/usr/bin/env bash
# Builds every Catch2 benchmark in tests/benchmarks for one side of the
# Benchmarks workflow (#276) and collects the binaries that side measures:
# the benchmarks, and logsquirl and logsquirl_grep for the e2e performance
# suite.
#
# Runs after docker-build, in the same container and build_root, so it only
# builds what ci_build did not: a benchmark the side's ci_build lacks, or, with
# the other side's tests/benchmarks copied in, the benchmarks from those
# sources. A benchmark that does not build is a warning on the before side
# (its sources may be newer than the code) and an error on the after side.
#
# Usage: build-benchmarks.sh before|after   (from the workspace root)
# Environment: CONTAINER (the image this side was built in), RUN_DIR, and
# LOGSQUIRL_WORKSPACE, LOGSQUIRL_BUILD_ROOT, LOGSQUIRL_VERSION as set by the
# composite actions.
set -euo pipefail

side=${1:?usage: build-benchmarks.sh before|after}
case "$side" in
    before) on_failure=warning ;;
    after) on_failure=error ;;
    *) echo "usage: build-benchmarks.sh before|after" >&2; exit 2 ;;
esac

targets=$(sed -n 's/^[[:space:]]*add_executable([[:space:]]*\([A-Za-z0-9_]*\).*/\1/p' tests/benchmarks/CMakeLists.txt | tr '\n' ' ')
if [ -z "${targets// /}" ]; then
    echo "::error::No benchmark targets in tests/benchmarks/CMakeLists.txt"
    exit 1
fi
echo "Benchmark targets ($side): $targets"

# The container runs as root; the run directory was created by the runner
# user, and the copies stay readable for it.
docker run --rm \
    --env LOGSQUIRL_VERSION="$LOGSQUIRL_VERSION" \
    --env BUILD_ROOT="$LOGSQUIRL_BUILD_ROOT" \
    --env BIN_DIR="$RUN_DIR/$side/bin" \
    --env TARGETS="$targets" \
    --env ON_FAILURE="$on_failure" \
    --env SIDE="$side" \
    -v "$LOGSQUIRL_WORKSPACE":/usr/local "$CONTAINER" \
    /bin/bash -c '
        set -uo pipefail
        cd /usr/local
        status=0
        for target in $TARGETS; do
            if cmake --build "$BUILD_ROOT" --target "$target" \
                    && cp "$BUILD_ROOT/output/$target" "$BIN_DIR/"; then
                :
            else
                echo "::$ON_FAILURE::$target does not build on the $SIDE side; it is not measured there"
                [ "$ON_FAILURE" = error ] && status=1
            fi
        done
        for binary in logsquirl logsquirl_grep; do
            cp "$BUILD_ROOT/output/$binary" "$BIN_DIR/" || status=1
        done
        # With Sentry, logsquirl starts its crash handler from its own
        # directory, which is part of what its startup costs.
        for helper in logsquirl_crashpad_handler; do
            [ ! -e "$BUILD_ROOT/output/$helper" ] || cp "$BUILD_ROOT/output/$helper" "$BIN_DIR/"
        done
        exit $status
    '
