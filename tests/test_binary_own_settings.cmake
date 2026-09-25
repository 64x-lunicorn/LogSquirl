# A test binary keeps its settings to itself (#370).
#
# Every test binary forces the portable settings, which are the `logsquirl.conf`
# beside the executable -- so a test binary run from `<build>/output/` used to
# read and write the very file the application, the command line tool and every
# other test binary read there. A test case that writes a setting and dies
# before restoring it then left that setting behind for all of them; #364 was a
# day spent on a leftover Encoding.
#
# This runs the test case that writes the most of them -- the Options Dialog
# saves what it writes to the application's settings file -- and checks that not
# one settings file beside the binary was created, changed or removed by it.
#
# Usage: cmake -DTEST_BINARY=<path to a test executable> -DTEST_SPEC=<Catch spec>
#              -P test_binary_own_settings.cmake

cmake_minimum_required(VERSION 3.16)

if(NOT TEST_BINARY OR NOT EXISTS "${TEST_BINARY}")
  message(FATAL_ERROR "TEST_BINARY is not a test executable: '${TEST_BINARY}'")
endif()
if(NOT TEST_SPEC)
  message(FATAL_ERROR "TEST_SPEC is not set")
endif()

get_filename_component(_binary_dir "${TEST_BINARY}" DIRECTORY)

# The settings files beside the binary, each with what is in it: a file that is
# added, removed or written shows up as a difference.
function(settings_files_state out_var)
  file(GLOB _settings_files "${_binary_dir}/*.conf")
  list(SORT _settings_files)
  set(_state "")
  foreach(_settings_file IN LISTS _settings_files)
    file(SHA256 "${_settings_file}" _hash)
    get_filename_component(_name "${_settings_file}" NAME)
    list(APPEND _state "${_name} ${_hash}")
  endforeach()
  set(${out_var} "${_state}" PARENT_SCOPE)
endfunction()

# A ThreadSanitizer build sorts the reports of what this starts the way the
# ctest runner does for every test case (#482); ignored by any other build.
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/TsanReportFilter.cmake")
get_filename_component(_tsan_dir "${_binary_dir}/../test_settings/own_settings_tsan" ABSOLUTE)
logsquirl_tsan_prepare("${_tsan_dir}")

settings_files_state(_before)

execute_process(
  COMMAND "${TEST_BINARY}" "${TEST_SPEC}" --warn UnmatchedTestSpec -platform offscreen
  WORKING_DIRECTORY "${_binary_dir}"
  OUTPUT_VARIABLE _stdout
  ERROR_VARIABLE _stderr
  RESULT_VARIABLE _result
  TIMEOUT 300
)

settings_files_state(_after)

logsquirl_tsan_check("${_tsan_dir}" _tsan_failures _tsan_output)
if(NOT _tsan_failures EQUAL 0)
  message(FATAL_ERROR "ThreadSanitizer: ${_tsan_failures} finding(s) in '${TEST_SPEC}':\n${_tsan_output}")
endif()
file(REMOVE_RECURSE "${_tsan_dir}")

if(NOT _result STREQUAL "0")
  message(FATAL_ERROR "'${TEST_SPEC}' exited with ${_result}\n"
                      "  stdout: ${_stdout}\n  stderr: ${_stderr}"
  )
endif()

if(NOT _before STREQUAL _after)
  list(JOIN _before "\n    " _before_report)
  list(JOIN _after "\n    " _after_report)
  message(FATAL_ERROR
          "'${TEST_SPEC}' wrote the settings beside the test binary in ${_binary_dir}\n"
          "  before:\n    ${_before_report}\n  after:\n    ${_after_report}"
  )
endif()

message(STATUS "the test binary left the settings beside it alone")
