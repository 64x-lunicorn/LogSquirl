# A test case that a signal ends fails with the signal's name, and what the
# ctest runner sets for the case still reaches it (#566).
#
# The runner used to start the case through `cmake -E env`, which reports how
# its child ended in its own words and then exits with 1: a case killed under
# `ctest -j8` failed with "the test case failed: 1" and nothing else. This runs
# a stand-in for a test binary through cmake/CatchTestDiscoveryRunTest.cmake
# three times: once it kills itself, and the runner has to name the signal;
# once it skips itself as Catch2 does, and the runner has to pass it (#633); once it
# prints the environment it was started with and leaves a ThreadSanitizer
# report where TSAN_OPTIONS says, and the runner has to have set the
# environment and sorted the report -- in every build, not only a TSan one.
#
# The stand-in is a shell script, so this is registered on Unix only.
#
# Usage: cmake -DRUNNER=<repo>/cmake/CatchTestDiscoveryRunTest.cmake
#              -DWORK_DIR=<scratch dir> -P case_runner_signal.cmake

cmake_minimum_required(VERSION 3.16)

if(NOT RUNNER OR NOT EXISTS "${RUNNER}")
  message(FATAL_ERROR "RUNNER is not the ctest runner: '${RUNNER}'")
endif()
if(NOT WORK_DIR)
  message(FATAL_ERROR "WORK_DIR is not set")
endif()
get_filename_component(_module_dir "${RUNNER}" DIRECTORY)

# The stand-in has a directory of its own: the runner links everything beside
# the binary into the case's directory.
file(REMOVE_RECURSE "${WORK_DIR}")
file(WRITE "${WORK_DIR}/source/fake_case" [=[#!/bin/sh
case "$1" in
  kill)
    echo "fake case: killing itself"
    kill -KILL $$
    ;;
  skip)
    echo "SKIPPED: fake case skips itself"
    exit 4
    ;;
  env)
    echo "isolated: $LOGSQUIRL_TEST_SETTINGS_ISOLATED"
    echo "tsan options: $TSAN_OPTIONS"
    log_path=$(printf '%s\n' "$TSAN_OPTIONS" | tr ':' '\n' | sed -n 's/^log_path=//p')
    printf 'WARNING: ThreadSanitizer: data race (fake case)\n' > "$log_path.$$"
    ;;
esac
]=])
file(COPY "${WORK_DIR}/source/fake_case" DESTINATION "${WORK_DIR}/bin"
     FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
set(_fake_case "${WORK_DIR}/bin/fake_case")

function(run_case mode)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DTEST_BINARY=${_fake_case}" -P "${RUNNER}" -- ${mode}
    OUTPUT_VARIABLE _stdout
    ERROR_VARIABLE _stderr
    RESULT_VARIABLE _result
    TIMEOUT 60
  )
  set(_result "${_result}" PARENT_SCOPE)
  set(_stdout "${_stdout}" PARENT_SCOPE)
  set(_output "${_stdout}${_stderr}" PARENT_SCOPE)
endfunction()

set(_failed "")

run_case(kill)
if(_result STREQUAL "0")
  string(APPEND _failed "\n  kill: the runner passed a case that was killed")
endif()
if(NOT _stdout MATCHES "fake case: killing itself")
  string(APPEND _failed "\n  kill: the case did not run")
endif()
# The words are CMake's for SIGKILL; the wrapper's "1" is what this replaces.
if(NOT _output MATCHES "the test case failed: Subprocess killed")
  string(APPEND _failed "\n  kill: the runner did not name the signal:\n${_output}")
endif()

# Catch2 ends with 4 when every case it ran skipped itself (#633): skipped,
# not failed, and said in the words ctest's SKIP_REGULAR_EXPRESSION looks for.
run_case(skip)
if(NOT _result STREQUAL "0")
  string(APPEND _failed "\n  skip: the runner failed a case that skipped itself:\n${_output}")
endif()
if(NOT _output MATCHES "LogSquirl test runner: the test case skipped itself")
  string(APPEND _failed "\n  skip: the runner did not say the case was skipped")
endif()

run_case(env)
if(_result STREQUAL "0")
  string(APPEND _failed "\n  env: the runner passed a case with a ThreadSanitizer report")
endif()
if(NOT _stdout MATCHES "isolated: 1")
  string(APPEND _failed "\n  env: LOGSQUIRL_TEST_SETTINGS_ISOLATED did not reach the case")
endif()
if(NOT _stdout MATCHES "tsan options: [^\n]*suppressions=${_module_dir}/tsan.supp")
  string(APPEND _failed "\n  env: the suppression file did not reach the case")
endif()
if(NOT _stdout MATCHES "tsan options: [^\n]*log_path=[^\n:]*/test_settings/[0-9a-f]+/tsan")
  string(APPEND _failed "\n  env: the log path in the case's directory did not reach the case")
endif()
if(NOT _stdout MATCHES "tsan options: [^\n]*exitcode=0")
  string(APPEND _failed "\n  env: exitcode=0 did not reach the case")
endif()
if(_output MATCHES "the test case failed:")
  string(APPEND _failed "\n  env: the case itself failed")
endif()
# CMake wraps the runner's message, so only its start is matched.
if(NOT _output MATCHES "ThreadSanitizer: 1 finding\\(s\\)")
  string(APPEND _failed "\n  env: the runner did not sort the report the case left")
endif()

if(_failed)
  message(FATAL_ERROR "The ctest runner did not run the case the way it should:${_failed}\n"
                      "  last output:\n${_output}")
endif()
message("Case runner: a signal is named, a skip is no failure, the environment reaches the case")
