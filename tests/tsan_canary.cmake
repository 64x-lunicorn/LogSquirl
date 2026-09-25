# A data race in LogSquirl's own code fails its test case (#439).
#
# The canary (tests/helpers/tsan_canary.cpp) increments a plain int from two
# threads and exits 0. It is run the way ctest runs every Catch2 case, through
# cmake/CatchTestDiscoveryRunTest.cmake, and this checks that the runner failed
# it for that race and for nothing else: TSan reported the race, the filter
# kept the report (its frames are in the canary, not in a library listed in
# cmake/tsan.supp), and the canary itself exited 0 -- so a crash, a timeout or
# a runner that fails every case does not count as the race being caught.
#
# Registered in a ThreadSanitizer build only. If this is red, a race in
# LogSquirl's code no longer turns the Sanitizers / tsan job red.
#
# Usage: cmake -DCANARY=<path to logsquirl_tsan_canary> -DRUNNER=<repo>/cmake/CatchTestDiscoveryRunTest.cmake
#              -P tsan_canary.cmake

cmake_minimum_required(VERSION 3.16)

if(NOT CANARY OR NOT EXISTS "${CANARY}")
  message(FATAL_ERROR "CANARY is not the canary executable: '${CANARY}'")
endif()
if(NOT RUNNER OR NOT EXISTS "${RUNNER}")
  message(FATAL_ERROR "RUNNER is not the ctest runner: '${RUNNER}'")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" "-DTEST_BINARY=${CANARY}" -P "${RUNNER}" --
  OUTPUT_VARIABLE _stdout
  ERROR_VARIABLE _stderr
  RESULT_VARIABLE _result
  TIMEOUT 120
)
set(_output "${_stdout}${_stderr}")

set(_failed "")
if(_result STREQUAL "0")
  string(APPEND _failed "\n  the runner passed the case")
endif()
if(NOT _stdout MATCHES "tsan canary: the two threads are done")
  string(APPEND _failed "\n  the canary did not run to its end")
endif()
if(_output MATCHES "the test case failed:")
  string(APPEND _failed "\n  the canary itself failed, not the race")
endif()
if(NOT _output MATCHES "WARNING: ThreadSanitizer: data race")
  string(APPEND _failed "\n  no data race report was kept")
endif()
if(NOT _output MATCHES "bumpUnguardedCounter")
  string(APPEND _failed "\n  the kept report does not name the canary's racing function")
endif()
# CMake wraps the runner's message, so only its start is matched.
if(NOT _output MATCHES "ThreadSanitizer: [1-9][0-9]* finding\\(s\\)")
  string(APPEND _failed "\n  the runner did not fail the case for TSan's findings")
endif()

if(_failed)
  message(FATAL_ERROR "The race in the canary did not fail its case the way it should:${_failed}\n"
                      "  runner exit: ${_result}\n  stdout:\n${_stdout}\n  stderr:\n${_stderr}")
endif()
message("TSan canary: the race in LogSquirl's code failed its case")
