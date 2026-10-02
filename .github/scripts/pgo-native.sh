#!/usr/bin/env bash
# Builds one variant of the PGO workflow (#682) on a macOS or Windows runner,
# and packs what the measure job runs: the plain build, or the instrumented
# build, its training, the merge and the optimized build (pgo.py).
#
# Writes pgo-run/pgo-<platform>-<variant>.tar with bin/, timings.json and
# times.md, and adds times.md to the job summary.
#
# Usage: pgo-native.sh clang|msvc   (from the workspace root)
# Environment: VARIANT (plain or pgo), TRAINING_RUNS, RUN_DIR and
# LOGSQUIRL_CMAKE_OPTS as set by prepare-workspace-env.
set -euo pipefail

toolchain=${1:?usage: pgo-native.sh clang|msvc}
case "$toolchain" in
    clang) platform=macos ;;
    msvc) platform=windows ;;
    *) echo "usage: pgo-native.sh clang|msvc" >&2; exit 2 ;;
esac

# CMake and pgo.py read forward slashes on Windows too; a backslash in the
# arguments would be taken for an escape.
if [ "${RUNNER_OS:-}" = Windows ]; then
    workspace=$(pwd -W)
else
    workspace=$(pwd)
fi
build="$RUN_DIR/build"
cmake_args="$LOGSQUIRL_CMAKE_OPTS -DCPM_SOURCE_CACHE=$workspace/cpm_cache"
scenario_targets=(logsquirl logsquirl_grep crashpad_handler)

pgo() {
    python .github/scripts/pgo.py --timings "$RUN_DIR/timings.json" "$@"
}

mkdir -p "$RUN_DIR"
case "$VARIANT" in
    plain)
        pgo build --build-dir "$build" --mode OFF --benchmarks --targets "${scenario_targets[@]}" \
            --cmake-args "$cmake_args"
        ;;
    pgo)
        pgo build --build-dir "$build" --mode GENERATE --targets "${scenario_targets[@]}" --cmake-args "$cmake_args"
        pgo train --binary-dir "$build/output" --profile-dir "$build/pgo-profile" --runs "$TRAINING_RUNS"
        pgo merge --toolchain "$toolchain" --profile-dir "$build/pgo-profile"
        pgo build --build-dir "$build" --mode USE --benchmarks --targets "${scenario_targets[@]}" \
            --cmake-args "$cmake_args"
        ;;
    *)
        echo "::error::VARIANT is '$VARIANT'; it is plain or pgo"
        exit 2
        ;;
esac

python .github/scripts/pgo.py collect --output "$build/output" --out-dir "$RUN_DIR/bin"
pgo times --title "$platform, $VARIANT" --markdown "$RUN_DIR/times.md"
# A tar keeps the executable bits and the app bundle's links, which an
# artifact loses.
tar -cf "$RUN_DIR/pgo-$platform-$VARIANT.tar" -C "$RUN_DIR" bin timings.json times.md
cat "$RUN_DIR/times.md" >> "$GITHUB_STEP_SUMMARY"
