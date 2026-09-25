#!/usr/bin/env bash
# TEMPORARY (#482): tells a hang from slowness for the test cases that time out
# under TSan. Runs each case alone with a long limit, takes a backtrace of every
# thread of it (and of its child processes) once it has run for PROBE_AFTER
# seconds, and reports how long it took in the end.
set -u

apt-get update -qq > /dev/null && apt-get install -y -qq gdb procps > /dev/null || echo "gdb install failed"

cd "/usr/local/$LOGSQUIRL_BUILD_ROOT/output" || exit 1
export TSAN_OPTIONS="suppressions=/usr/local/cmake/tsan.supp"
# Settings beside the binary are fine in a throw-away container; removed below.
export LOGSQUIRL_TEST_SETTINGS_ISOLATED=1
PROBE_AFTER=75
LIMIT=600

descendants() {
    local p
    for p in $(pgrep -P "$1"); do
        echo "$p"
        descendants "$p"
    done
}

probe() {
    local binary="$1" name="$2" tag="$3"
    local log="/usr/local/$LOGSQUIRL_BUILD_ROOT/probe-$tag.log"
    local start
    start=$(date +%s)
    "./$binary" "$name" -platform offscreen > "$log" 2>&1 &
    local pid=$!
    local waited=0
    while kill -0 "$pid" 2> /dev/null && [ "$waited" -lt "$PROBE_AFTER" ]; do
        sleep 1
        waited=$((waited + 1))
    done
    if kill -0 "$pid" 2> /dev/null; then
        {
            echo "=== [$tag] still running after $PROBE_AFTER s"
            ps -eo pid,ppid,stat,wchan:32,etime,args --forest | grep -v ' ps -eo'
            for p in "$pid" $(descendants "$pid"); do
                echo "=== [$tag] /proc/$p: $(tr '\0' ' ' < "/proc/$p/cmdline" 2> /dev/null)"
                for t in /proc/"$p"/task/*; do
                    echo "  task ${t##*/}: $(cat "$t/comm" 2> /dev/null) state=$(awk '{print $3}' "$t/stat" 2> /dev/null) wchan=$(cat "$t/wchan" 2> /dev/null)"
                done
                timeout 120 gdb -p "$p" -batch -nx -ex "set pagination off" -ex "thread apply all bt 25" 2>&1 |
                    grep -v '^\[New LWP\|^warning: \|^Download' | head -400
            done
        } > "$log.bt" 2>&1
    fi
    local status=0
    while kill -0 "$pid" 2> /dev/null && [ "$waited" -lt "$LIMIT" ]; do
        sleep 1
        waited=$((waited + 1))
    done
    if kill -0 "$pid" 2> /dev/null; then
        echo "=== [$tag] HANG: still running after $LIMIT s, killed" >> "$log.bt"
        for p in $(descendants "$pid"); do kill -9 "$p"; done
        kill -9 "$pid"
        wait "$pid" 2> /dev/null
        status=killed
    else
        wait "$pid"
        status=$?
    fi
    echo "=== [$tag] '$name' ended after $(($(date +%s) - start)) s with $status" >> "$log.bt"
}

probe logsquirl_tests "A Team Folder without Git reports that Git is missing" teamfolder-missing-git &
probe logsquirl_tests "A publish that stops before committing is answered\\, not lost" teamfolder-publish-stops &
wait
probe logsquirl_itests "Scenario: An explicit reload notices a Log File rewritten in place with the same size" itests-reload
probe logsquirl_itests "Scenario: Following a growing Log File reads only what was appended" itests-follow
probe logsquirl_itests "Scenario: Reopening a grown Log File indexes only what was added to it" itests-reopen

rm -f ./*.conf
for f in /usr/local/"$LOGSQUIRL_BUILD_ROOT"/probe-*.log; do
    echo "################ $f"
    grep -v '^ *#[0-9]\|^$' "$f" | tail -60
    echo "################ $f.bt"
    cat "$f.bt" 2> /dev/null
done
exit 0
