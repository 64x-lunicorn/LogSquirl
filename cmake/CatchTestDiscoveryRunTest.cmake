# Runs one Catch2 test case beside a settings file of its own (#370).
#
# A test binary forces the portable settings, which are the `logsquirl.conf`
# beside the executable -- so every test binary run from `<build>/output/` read
# and wrote the one file there, the same one the application and the command
# line tool read. A case that writes a setting and dies before restoring it left
# it behind for every later case, in that run and in every run afterwards; #364
# was a day spent on a leftover Encoding.
#
# So the executable moves: the test case runs as a hard link to the binary in a
# scratch directory that is emptied before the case starts and removed after it,
# and the settings file is created there. Nothing restores anything, so nothing
# depends on a case surviving. Hard links cost a directory entry, not the bytes
# of a Debug test binary, and the scratch directory is a sibling of the built
# binaries so that it is on their file system.
#
# The test binaries isolate themselves the same way when they are run directly
# (tests/helpers/isolated_settings.h) -- LOGSQUIRL_TEST_SETTINGS_ISOLATED tells
# them that this has already been done for them, which saves a second start of
# the binary for every one of the several hundred test cases.
#
# Usage: cmake -DTEST_BINARY=<path to the test executable>
#              -P CatchTestDiscoveryRunTest.cmake -- <argument>...

cmake_minimum_required(VERSION 3.16)

if(NOT TEST_BINARY OR NOT EXISTS "${TEST_BINARY}")
  message(FATAL_ERROR "TEST_BINARY is not a test executable: '${TEST_BINARY}'")
endif()

# Everything after "--" is what the test case is run with.
set(_arguments "")
set(_after_separator FALSE)
math(EXPR _last_argument "${CMAKE_ARGC} - 1")
foreach(_index RANGE 0 ${_last_argument})
  set(_argument "${CMAKE_ARGV${_index}}")
  if(_after_separator)
    list(APPEND _arguments "${_argument}")
  elseif(_argument STREQUAL "--")
    set(_after_separator TRUE)
  endif()
endforeach()

get_filename_component(_binary_dir "${TEST_BINARY}" DIRECTORY)
get_filename_component(_binary_name "${TEST_BINARY}" NAME)

# One directory per test case, named after what the case is run with: two cases
# never share one, so a run with `ctest -j` isolates them as a serial run does.
string(MD5 _case_id "${_binary_name} ${_arguments}")
set(_work_dir "${_binary_dir}/../test_settings/${_case_id}")

# Emptied before the case, not after it: what a case that died left behind is
# gone before the next run of that case reads anything.
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")

file(CREATE_LINK "${TEST_BINARY}" "${_work_dir}/${_binary_name}" COPY_ON_ERROR)

# The neighbours of the binary, so that a test finds what it runs or loads
# beside itself: the helper that writes a Log File, the command line tool, the
# libraries Windows looks for there. A link or nothing -- no run pays for a copy
# of everything that was built. The settings files are what this is about and
# are the one thing left behind.
file(GLOB _neighbours "${_binary_dir}/*")
foreach(_neighbour IN LISTS _neighbours)
  get_filename_component(_neighbour_name "${_neighbour}" NAME)
  if(IS_DIRECTORY "${_neighbour}"
     OR _neighbour_name STREQUAL _binary_name
     OR _neighbour_name MATCHES "\\.conf$")
    continue()
  endif()
  file(CREATE_LINK "${_neighbour}" "${_work_dir}/${_neighbour_name}" RESULT _link_result)
endforeach()

# No OUTPUT_VARIABLE: what the test case prints is what this script prints, so
# ctest reads it as it always did, as it is printed.
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env "LOGSQUIRL_TEST_SETTINGS_ISOLATED=1"
          -- "${_work_dir}/${_binary_name}" ${_arguments}
  RESULT_VARIABLE _result
)

file(REMOVE_RECURSE "${_work_dir}")

# A signal is reported as its name, so a case that crashed says so instead of
# ending in an exit code nobody can read.
if(NOT _result STREQUAL "0")
  message(FATAL_ERROR "the test case failed: ${_result}")
endif()
