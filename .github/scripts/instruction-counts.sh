#!/usr/bin/env bash
# Builds the Catch2 benchmarks of the checked-out commit and counts the
# instructions of each benchmark under Callgrind (#671), and in the same run
# its allocations and peak heap (#673).
#
# Runs where the benchmarks run: in the Ubuntu 24.04 build container with
# Valgrind, from the workspace root (/usr/local in the container), as the
# Instruction Counts workflow does once per side, or on a Linux machine with
# Valgrind and its headers (valgrind/callgrind.h; BUILD.md, "Instruction
# counts"). Valgrind has no arm64 macOS port.
#
# Every benchmark target in tests/benchmarks/CMakeLists.txt is built in
# BUILD_ROOT, configured with CMAKE_OPTS and LOGSQUIRL_BENCHMARK_HEAP_COUNTS=ON,
# which links tests/benchmarks/heap_count.c into each: it replaces glibc's
# malloc and wraps mimalloc's mi_new_n and mi_free to count the heap blocks the
# measured code allocates and the most heap it holds at once. It adds no run
# and no compiled object outside tests/benchmarks, so sccache's hits stay.
# Then each binary runs once under Callgrind in the benchmarks' fixed-work mode
# (tests/benchmarks/instruction_count.h): with instrumentation off until a
# benchmark's measured code starts, which keeps the rest of the binary (writing
# its Log Files, starting Qt) at about a quarter of native speed instead of a
# fiftieth. The test cases run in declaration order with a fixed seed and a
# fixed QHash seed, so every run does the same work; test cases tagged
# [wall-clock] say something only as a time (they time themselves, or their
# work is another process's) and are left out.
#
# For each binary, <results>/<binary>/ gets the Callgrind dumps, the heap
# counts (heap.tsv), the binary's output (log.txt) and, once it has ended, its
# exit code (exit_code; "not built" for a target that did not build).
# instruction-counts.py collect reads that directory.
#
# With RECOUNT_TARGETS, it builds nothing and counts only those benchmark
# targets once more, from the build the same build directory holds: the
# Instruction Counts workflow recounts the after side's binaries that have a
# benchmark over its threshold (#708).
#
# Usage: instruction-counts.sh <results directory>
# Environment:
#   BUILD_ROOT       build directory (default build_root)
#   CMAKE_OPTS       configure options (default: RelWithDebInfo with Ninja, as
#                    the release builds; the workflow passes the CI Build noble
#                    job's options so the sccache cache fits)
#   RECOUNT_TARGETS  benchmark targets to count again without building, by
#                    name, separated by white space
#   BINARY_TIMEOUT   seconds one binary may take under Callgrind (default 1800)
#   LOGSQUIRL_BENCHMARK_LOG_FILE_MB, LOGSQUIRL_BENCHMARK_SESSION_LOG_FILE_MB:
#                    the size of the generated Log Files (default 4 and 4 MiB)
set -euo pipefail

results=${1:?usage: instruction-counts.sh <results directory>}
build_root=${BUILD_ROOT:-build_root}
cmake_opts=${CMAKE_OPTS:--G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo}
binary_timeout=${BINARY_TIMEOUT:-1800}

if ! command -v valgrind > /dev/null; then
    echo "::error::Valgrind is not installed"
    exit 1
fi

targets=$(sed -n 's/^[[:space:]]*add_executable([[:space:]]*\([A-Za-z0-9_]*\).*/\1/p' tests/benchmarks/CMakeLists.txt)
if [ -z "$targets" ]; then
    echo "::error::No benchmark targets in tests/benchmarks/CMakeLists.txt"
    exit 1
fi

grep -m 1 '^model name' /proc/cpuinfo || true
valgrind --version

# configure_and_build: every target in $targets, in $build_root.
configure_and_build() {
    echo "::group::Configure"
    # shellcheck disable=SC2086 # the options are a list of words
    cmake -B "$build_root" $cmake_opts -DLOGSQUIRL_BENCHMARK_HEAP_COUNTS=ON .
    echo "::endgroup::"

    # A target that does not build must not be counted from an earlier build in
    # the same build directory (the workflow builds both sides in one).
    for target in $targets; do
        rm -f "$build_root/output/$target"
    done

    echo "::group::Build"
    local build_start
    build_start=$(date +%s)
    # shellcheck disable=SC2086 # one word per target
    if ! cmake --build "$build_root" --target $targets -- -k 0; then
        echo "::warning::Not every benchmark built; the ones that did not are reported as failed"
    fi
    echo "::endgroup::"
    echo "Build: $(( $(date +%s) - build_start )) s"
    if [ -n "${SCCACHE_DIR:-}" ] && command -v sccache > /dev/null; then
        sccache --show-stats || true
    fi
}

if [ -n "${RECOUNT_TARGETS:-}" ]; then
    recount=""
    for target in $RECOUNT_TARGETS; do
        if ! printf '%s\n' "$targets" | grep -qxF -- "$target"; then
            echo "::error::$target is not a benchmark target"
            exit 1
        fi
        recount="$recount $target"
    done
    targets=$recount
    echo "Recounting, without building:$targets"
else
    configure_and_build
fi

export LOGSQUIRL_BENCHMARK_COUNT_INSTRUCTIONS=1
export QT_HASH_SEED=0
export QT_QPA_PLATFORM=offscreen
export LC_ALL=C.UTF-8
export LOGSQUIRL_BENCHMARK_LOG_FILE_MB=${LOGSQUIRL_BENCHMARK_LOG_FILE_MB:-4}
export LOGSQUIRL_BENCHMARK_SESSION_LOG_FILE_MB=${LOGSQUIRL_BENCHMARK_SESSION_LOG_FILE_MB:-4}

# count <target> <output directory>
count() {
    local target=$1 out=$2
    rm -rf "$out"
    mkdir -p "$out"
    if [ ! -x "$build_root/output/$target" ]; then
        echo "::warning::$target did not build"
        echo "not built" > "$out/exit_code"
        return
    fi
    echo "::group::$target"
    local start code=0 heap_file
    start=$(date +%s)
    # Absolute: the binary relaunches itself, and writes it from the relaunch.
    heap_file="$(cd "$out" && pwd)/heap.tsv"
    # --trace-children: a binary with a settings file of its own relaunches
    # itself (tests/helpers/isolated_settings.h), and the relaunched process is
    # the one that runs the benchmarks. A child a benchmark starts (the command
    # line tool) runs with instrumentation off, so its own instructions are not
    # counted. A larger main stack than the 8 MiB Valgrind gives by default:
    # the generated Log Lines of some benchmarks are built on it.
    # --fair-sched: the threads take their turns in a fixed order, which made
    # the counts of the benchmarks that wait for other threads repeat about
    # three times as closely in #671.
    LOGSQUIRL_BENCHMARK_HEAP_FILE="$heap_file" \
    timeout "$binary_timeout" valgrind --tool=callgrind --instr-atstart=no --trace-children=yes \
        --main-stacksize=67108864 --fair-sched=yes \
        --callgrind-out-file="$out/callgrind.out.%p" \
        "$build_root/output/$target" --order decl --rng-seed 1 '~[wall-clock]' --allow-running-no-tests \
        > "$out/log.txt" 2>&1 || code=$?
    tail -n 20 "$out/log.txt"
    echo "::endgroup::"
    echo "$code" > "$out/exit_code"
    echo "$target: exit code $code, $(( $(date +%s) - start )) s"
    if [ "$code" != 0 ]; then
        echo "::warning::$target exited with $code under Callgrind"
    fi
}

mkdir -p "$results"
for target in $targets; do
    count "$target" "$results/$target"
done

