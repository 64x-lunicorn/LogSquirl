#!/usr/bin/env bash
# Runs clang-tidy with the repository's .clang-tidy over the project sources a
# change touches, and fails on any finding (#440).
#
#   .github/scripts/run-clang-tidy.sh BUILD_DIR [changed|all]
#
# BUILD_DIR is a configured CMake build directory with a compile_commands.json
# (CMake writes one, see cmake/StandardProjectSettings.cmake). The Qt code
# generators (moc, uic) run first, so the generated headers the sources include
# exist; nothing is compiled or linked. It needs Ninja (`-G Ninja`).
#
# changed (default): the .cpp files under src/ that the last commit changed
#   (HEAD^1..HEAD: a pull request's merge commit against its base, or a push),
#   plus, for a changed header, every .cpp of the same module (src/<module>/).
#   A change to .clang-tidy or to this script selects all of them.
# all: every .cpp under src/ (manual runs, workflow_dispatch).
#
# Tests are not analysed: their Catch2 macros are outside what .clang-tidy was
# tuned for. CLANG_TIDY names the binary (default clang-tidy), TIDY_ARGS adds
# arguments to it (e.g. --extra-arg=-isysroot... on macOS), JOBS is the number
# of parallel runs (default: all cores).
set -euo pipefail

build=${1:?usage: run-clang-tidy.sh BUILD_DIR [changed|all]}
mode=${2:-changed}
tidy=${CLANG_TIDY:-clang-tidy}
jobs=${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu)}

cd "$(git rev-parse --show-toplevel)"
root=$PWD

if [ ! -f "$build/compile_commands.json" ]; then
    echo "::error::$build/compile_commands.json not found; configure the build first"
    exit 2
fi

all_sources() {
    git ls-files -- 'src/*.cpp'
}

selected=$(mktemp)
trap 'rm -f "$selected"' EXIT

if [ "$mode" = all ]; then
    all_sources > "$selected"
else
    changed=$(git diff --name-only --diff-filter=d HEAD^1 HEAD -- \
        'src/*.cpp' 'src/*.h' 'src/*.hpp' .clang-tidy .github/scripts/run-clang-tidy.sh)
    if grep -qxE '\.clang-tidy|\.github/scripts/run-clang-tidy\.sh' <<<"$changed"; then
        all_sources > "$selected"
    else
        while IFS= read -r file; do
            case "$file" in
                *.cpp) echo "$file" ;;
                src/*/*.h | src/*/*.hpp)
                    module=$(cut -d/ -f1-2 <<<"$file")
                    git ls-files -- "$module/*.cpp"
                    ;;
            esac
        done <<<"$changed" | sort -u > "$selected"
    fi
fi

# Only translation units this platform's build has (a file may be another
# platform's, or a test helper).
files=()
while IFS= read -r file; do
    if grep -qF "\"$root/$file\"" "$build/compile_commands.json"; then
        files+=("$file")
    fi
done < "$selected"

if [ "${#files[@]}" -eq 0 ]; then
    echo "No C++ sources under src/ to analyse."
    exit 0
fi
echo "clang-tidy ($("$tidy" --version | grep -m1 -i version)) over ${#files[@]} file(s):"
printf '  %s\n' "${files[@]}"

# generated/version.h, which logsquirl_version includes.
ninja -C "$build" generate_version > /dev/null

# The moc/uic run of every module, so ui_*.h and *.moc exist. Building the
# _autogen targets would also build everything they depend on (the CPM
# packages, the libraries: ~500 compilations); the command each one runs is
# just `cmake -E cmake_autogen`, so those are run directly.
generators=()
while IFS= read -r target; do
    generators+=("$target")
done < <(ninja -C "$build" -t targets all |
    sed -nE 's#^((src|3rdparty)/[^:]*_autogen): phony$#\1#p' | { grep -v CMakeFiles || true; } | sort -u)
if [ "${#generators[@]}" -gt 0 ]; then
    autogen=$(mktemp)
    ninja -C "$build" -t commands "${generators[@]}" | { grep 'cmake_autogen' || true; } | sort -u > "$autogen"
    if ! sh -e "$autogen" > "$autogen.log" 2>&1; then
        cat "$autogen.log"
        echo "::error::the Qt code generators failed"
        rm -f "$autogen" "$autogen.log"
        exit 2
    fi
    rm -f "$autogen" "$autogen.log"
fi

status=0
printf '%s\0' "${files[@]}" |
    xargs -0 -n 1 -P "$jobs" "$tidy" -p "$build" --quiet --warnings-as-errors='*' \
        --extra-arg=-Wno-unknown-warning-option --extra-arg=-Wno-unused-command-line-argument \
        ${TIDY_ARGS:-} ||
    status=$?

if [ "$status" -ne 0 ]; then
    echo "::error::clang-tidy found problems; see .clang-tidy for the checks and how a finding is silenced"
fi
exit "$status"
