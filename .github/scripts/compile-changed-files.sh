#!/usr/bin/env bash
# Compiles the project's C++ sources a change touches, each with the command
# the configured build holds for it in compile_commands.json, so a diagnostic
# only this compiler reports (GCC's -Werror=range-loop-construct broke every
# Linux job of #752 a quarter of an hour in) fails a pull request within
# minutes, before the full build reaches the file (#759, #760).
#
#   .github/scripts/compile-changed-files.sh BUILD_DIR [changed|all|FILE...]
#
# BUILD_DIR is a configured CMake build directory with a compile_commands.json
# (CMake writes one, see cmake/StandardProjectSettings.cmake). Nothing is
# linked and no dependency is built: the version header and the Qt code
# generators (moc, uic) run first, then each selected source is compiled by
# its own compile command, so the flags, defines and include paths are the
# build's own and cannot drift from it. It needs Ninja (`-G Ninja`) and
# python3, which reads the compile database.
#
# changed (default): the .cpp files under src/ and tests/ that the last commit
#   changed (HEAD^1..HEAD: a pull request's merge commit against its base, or
#   a push), plus, for a changed header, every .cpp under src/ and tests/ that
#   includes it by name and every .cpp of the same module (src/<module>/,
#   tests/<suite>/). A change to this script selects all of them.
# all: every .cpp under src/ and tests/ (manual runs, workflow_dispatch).
# FILE...: the given sources, as paths from the repository root.
#
# Only translation units this build compiles are taken: a file of another
# platform, or of an option that is off, is not in compile_commands.json and
# is skipped. JOBS is the number of parallel compilations (default: all cores).
# Every phase prints how long it took, so the job's time can be read off.
set -euo pipefail

build=${1:?usage: compile-changed-files.sh BUILD_DIR [changed|all|FILE...]}
shift
mode=${1:-changed}
jobs=${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu)}

cd "$(git rev-parse --show-toplevel)"
root=$PWD

if [ ! -f "$build/compile_commands.json" ]; then
    echo "::error::$build/compile_commands.json not found; configure the build first"
    exit 2
fi

phase_start=$SECONDS
phase() { # name: prints the time since the previous phase() call
    echo "$1: $(( SECONDS - phase_start )) s"
    phase_start=$SECONDS
}

all_sources() {
    git ls-files -- 'src/*.cpp' 'tests/*.cpp'
}

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
selected=$work/selected

case "$mode" in
    all)
        all_sources > "$selected"
        ;;
    changed)
        changed=$(git diff --name-only --diff-filter=d HEAD^1 HEAD -- \
            'src/*.cpp' 'src/*.h' 'src/*.hpp' 'tests/*.cpp' 'tests/*.h' 'tests/*.hpp' \
            .github/scripts/compile-changed-files.sh)
        if grep -qxF '.github/scripts/compile-changed-files.sh' <<<"$changed"; then
            all_sources > "$selected"
        else
            # The changed sources first, so the error a change itself holds
            # is reported before the files a changed header pulls in.
            grep -E '\.cpp$' <<<"$changed" | sort -u > "$work/direct" || true
            while IFS= read -r file; do
                case "$file" in
                    *.h | *.hpp)
                        # The header's module, and whoever includes it by name
                        # from another one; what includes it through a third
                        # header is caught by the full build, as before.
                        module=$(cut -d/ -f1-2 <<<"$file")
                        git ls-files -- "$module/*.cpp"
                        name=$(basename "$file" | sed 's/[.[\*^$]/\\&/g')
                        git grep -lE "^[[:space:]]*#[[:space:]]*include[[:space:]]*[<\"]([^>\"]*/)?${name}[>\"]" \
                            -- 'src/*.cpp' 'tests/*.cpp' || true
                        ;;
                esac
            done <<<"$changed" | sort -u | { grep -vxF -f "$work/direct" || true; } > "$work/derived"
            cat "$work/direct" "$work/derived" > "$selected"
        fi
        ;;
    *)
        printf '%s\n' "$mode" "$@" | sort -u > "$selected"
        ;;
esac

# One file per compile command of a selected source this build has, holding
# its directory, output, source and command on four lines, numbered in the
# order the sources were selected.
mkdir "$work/commands"
python3 - "$build/compile_commands.json" "$selected" "$root" "$work/commands" <<'EOF'
import json, os, sys
database, selected, root, out = sys.argv[1:]
with open(selected) as f:
    order = [os.path.join(root, line.strip()) for line in f if line.strip()]
with open(database) as f:
    by_file = {e["file"]: e for e in json.load(f)}
entries = [by_file[path] for path in order if path in by_file]
for i, e in enumerate(entries):
    with open(os.path.join(out, f"{i:04d}"), "w") as f:
        f.write("\n".join([e["directory"], e["output"], e["file"], e["command"]]) + "\n")
EOF

count=$(find "$work/commands" -type f | wc -l)
if [ "$count" -eq 0 ]; then
    echo "No C++ sources of this build under src/ or tests/ to compile."
    exit 0
fi
echo "Compiling $count file(s) with the build's own compile commands:"
for entry in "$work"/commands/*; do
    sed -n '3p' "$entry" | sed "s#^$root/#  #"
done
phase "Selecting the files"

# generated/version.h, which logsquirl_version includes.
ninja -C "$build" generate_version > /dev/null
phase "Generating the version header"

# The moc/uic run of every module and test suite, so ui_*.h and *.moc exist.
# Building the _autogen targets would also build everything they depend on
# (the CPM packages, the libraries: ~500 compilations); the commands each one
# runs are `cmake -E cmake_autogen` and the compiler run that writes
# moc_predefs.h, so those are run directly.
generators=()
while IFS= read -r target; do
    generators+=("$target")
done < <(ninja -C "$build" -t targets all |
    sed -nE 's#^((src|tests|3rdparty)/[^:]*_autogen): phony$#\1#p' | { grep -v CMakeFiles || true; } | sort -u)
if [ "${#generators[@]}" -gt 0 ]; then
    autogen=$work/autogen.sh
    ninja -C "$build" -t commands "${generators[@]}" |
        { grep -E 'cmake_autogen|moc_predefs\.h' || true; } | sort -u > "$autogen"
    echo "Running ${#generators[@]} Qt code generator target(s), $(wc -l < "$autogen") command(s)"
    if ! (cd "$build" && sh -e "$autogen" > "$autogen.log" 2>&1); then
        cat "$autogen.log"
        echo "::error::the Qt code generators failed"
        exit 2
    fi
fi
phase "Running the Qt code generators"

# Each compile in its own directory with its own command, JOBS at a time; the
# output of a failed one is printed whole, under the file's name. A failure
# returns 255, on which xargs starts no further compile (the running ones
# finish), as the full build would stop at its first error.
compile_one() {
    local directory output file command log status=0 started=$SECONDS
    { read -r directory; read -r output; read -r file; read -r command; } < "$1"
    log=$1.log
    mkdir -p "$directory/$(dirname "$output")"
    (cd "$directory" && sh -c "$command") > "$log" 2>&1 || status=$?
    if [ "$status" -ne 0 ]; then
        echo "::group::FAILED ${file#"$ROOT"/} ($(( SECONDS - started )) s)"
        cat "$log"
        echo "::endgroup::"
        return 255
    fi
    echo "compiled ${file#"$ROOT"/} ($(( SECONDS - started )) s)"
    if [ -s "$log" ]; then
        cat "$log"
    fi
}
export -f compile_one
export ROOT=$root

status=0
find "$work/commands" -type f -print0 | sort -z |
    xargs -0 -n 1 -P "$jobs" bash -c 'compile_one "$0"' || status=$?
phase "Compiling with $jobs job(s)"

if [ "$status" -ne 0 ]; then
    echo "::error::GCC rejected a changed file; the full build would fail the same way"
    exit 1
fi
