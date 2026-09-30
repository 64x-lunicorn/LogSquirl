#!/usr/bin/env bash
# Counts one side of the Instruction Counts workflow (#671) on the runner:
# makes sure the side's build container has Valgrind, runs
# instruction-counts.sh in it on the checked-out commit, and collects the
# counts into <RUN_DIR>/report/<side>.json.
#
# Valgrind comes from docker/ubuntu24.04 once an image with it is published;
# an older image (the before side of the pull request that added it, or a
# pull request's own image before it is merged) gets it from Ubuntu's archive
# in a derived image that lives on this runner only.
#
# The build uses the CI Build noble job's configure options and its sccache
# cache (restored by the workflow, never saved from here), in the same
# build_root for both sides, so the after side only rebuilds what differs.
#
# Usage: instruction-counts-side.sh before|after   (from the workspace root)
# Environment: RUN_DIR, LOGSQUIRL_CONTAINER, LOGSQUIRL_WORKSPACE,
# LOGSQUIRL_BUILD_ROOT, LOGSQUIRL_CMAKE_OPTS and LOGSQUIRL_VERSION, as the
# composite actions set them.
set -euo pipefail

side=${1:?usage: instruction-counts-side.sh before|after}
case "$side" in
    before | after) ;;
    *) echo "usage: instruction-counts-side.sh before|after" >&2; exit 2 ;;
esac

image="logsquirl-instruction-counts:$side"
echo "::group::Valgrind in $LOGSQUIRL_CONTAINER"
docker build --tag "$image" - <<EOF
FROM $LOGSQUIRL_CONTAINER
RUN command -v valgrind || (apt-get update -y \
    && DEBIAN_FRONTEND=noninteractive apt-get install --no-install-recommends -y valgrind \
    && apt-get clean && rm -rf /var/lib/apt/lists/*)
EOF
docker run --rm "$image" valgrind --version
echo "::endgroup::"

start=$(date +%s)
status=0
# shellcheck disable=SC2016 # expanded by the shell in the container
docker run --rm \
    --env LOGSQUIRL_VERSION="$LOGSQUIRL_VERSION" \
    --env BUILD_ROOT="$LOGSQUIRL_BUILD_ROOT" \
    --env CMAKE_OPTS="$LOGSQUIRL_CMAKE_OPTS -DCPM_SOURCE_CACHE=/usr/local/cpm_cache -DCMAKE_C_COMPILER_LAUNCHER=sccache -DCMAKE_CXX_COMPILER_LAUNCHER=sccache" \
    --env SCCACHE_DIR=/usr/local/sccache_cache \
    --env SCCACHE_CACHE_SIZE=2G \
    --workdir /usr/local \
    -v "$LOGSQUIRL_WORKSPACE":/usr/local "$image" /bin/bash -c '
        status=0
        "$1" "$2" || status=$?
        sccache --show-stats || true
        exit $status
    ' counts "/usr/local/$RUN_DIR/tools/.github/scripts/instruction-counts.sh" "$RUN_DIR/dumps/$side" \
    || status=$?
echo "The $side side took $(( $(date +%s) - start )) s to build and count"

python3 "$RUN_DIR/tools/.github/scripts/instruction-counts.py" collect "$RUN_DIR/dumps/$side" \
    --json "$RUN_DIR/report/$side.json"
jq -r '.benchmarks[] | "\(.instructions)\t\(.binary): \(.name)"' "$RUN_DIR/report/$side.json"
exit $status
