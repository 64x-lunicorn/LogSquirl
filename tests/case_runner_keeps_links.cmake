# The ctest runner keeps a test case's links to the built executables and
# throws away everything else the case left (#566).
#
# On macOS, removing one hard link of an executable can get a process that is
# starting that same executable under another name killed with SIGKILL. The
# runner used to remove the case's directory, links included, after every case,
# and with `ctest -j8` about one case in two hundred died that way before it
# printed anything. So a link to the current binary stays from one run of the
# case to the next; what the case wrote beside it -- its settings files above
# all (#370) -- is still gone before the next run, and a rebuilt binary is
# linked anew.
#
# The stand-in for a test binary is a shell script, so this is registered on
# Unix only.
#
# Usage: cmake -DRUNNER=<repo>/cmake/CatchTestDiscoveryRunTest.cmake
#              -DWORK_DIR=<scratch dir> -P case_runner_keeps_links.cmake

cmake_minimum_required(VERSION 3.16)

if(NOT RUNNER OR NOT EXISTS "${RUNNER}")
  message(FATAL_ERROR "RUNNER is not the ctest runner: '${RUNNER}'")
endif()
if(NOT WORK_DIR)
  message(FATAL_ERROR "WORK_DIR is not set")
endif()

# The stand-in says which build it is, whether it found what an earlier run
# left, and whether its neighbour is beside it; then it leaves a settings file
# and a directory of its own.
function(write_fake_case version)
  file(WRITE "${WORK_DIR}/next/fake_case" "#!/bin/sh\necho \"fake case: build ${version}\"\n" [=[
here=$(dirname "$0")
[ -e "$here/logsquirl.conf" ] && echo "fake case: found a settings file"
[ -e "$here/left/behind" ] && echo "fake case: found what an earlier run left"
[ -x "$here/helper" ] && echo "fake case: the helper is beside it"
echo "Encoding=leftover" > "$here/logsquirl.conf"
mkdir -p "$here/left" && touch "$here/left/behind"
exit 0
]=])
  # Written beside and renamed over, as a linker does: a rebuilt binary is a
  # new file with a time stamp of its own, not the old one changed. Not
  # file(COPY), which would round the time stamp to the second of the build
  # before.
  execute_process(COMMAND chmod u+x "${WORK_DIR}/next/fake_case")
  file(RENAME "${WORK_DIR}/next/fake_case" "${WORK_DIR}/bin/fake_case")
endfunction()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}/bin")
file(WRITE "${WORK_DIR}/source/helper" "#!/bin/sh\n")
file(COPY "${WORK_DIR}/source/helper" DESTINATION "${WORK_DIR}/bin"
     FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
write_fake_case(1)

function(run_case)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DTEST_BINARY=${WORK_DIR}/bin/fake_case" -P "${RUNNER}" -- a-case
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

# What is in the case's directory once the case has ended.
function(check_case_dir run)
  file(GLOB _case_dirs LIST_DIRECTORIES true "${WORK_DIR}/test_settings/*")
  list(LENGTH _case_dirs _count)
  if(NOT _count EQUAL 1)
    string(APPEND _failed "\n  ${run}: expected one case directory, found ${_count}")
    set(_failed "${_failed}" PARENT_SCOPE)
    return()
  endif()
  file(GLOB _left RELATIVE "${_case_dirs}" LIST_DIRECTORIES true "${_case_dirs}/*")
  list(SORT _left)
  if(NOT _left STREQUAL "fake_case;helper")
    string(APPEND _failed "\n  ${run}: the case's directory holds '${_left}', not the two links")
  endif()
  set(_failed "${_failed}" PARENT_SCOPE)
endfunction()

run_case()
if(NOT _result STREQUAL "0" OR NOT _stdout MATCHES "fake case: build 1")
  string(APPEND _failed "\n  first run: the case did not pass:\n${_output}")
endif()
if(NOT _stdout MATCHES "the helper is beside it")
  string(APPEND _failed "\n  first run: the neighbour was not linked beside the case")
endif()
check_case_dir("first run")

run_case()
if(NOT _result STREQUAL "0" OR NOT _stdout MATCHES "fake case: build 1")
  string(APPEND _failed "\n  second run: the case did not pass:\n${_output}")
endif()
if(_stdout MATCHES "found a settings file|found what an earlier run left")
  string(APPEND _failed "\n  second run: the case found what the first one left:\n${_stdout}")
endif()
check_case_dir("second run")

write_fake_case(2)
run_case()
if(NOT _result STREQUAL "0" OR NOT _stdout MATCHES "fake case: build 2")
  string(APPEND _failed "\n  after a rebuild: the case did not run the rebuilt binary:\n${_output}")
endif()
check_case_dir("after a rebuild")

if(_failed)
  message(FATAL_ERROR "The ctest runner did not keep the case's links the way it should:${_failed}")
endif()
message("Case runner: the links stay, what the case left goes")
