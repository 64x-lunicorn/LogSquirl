# Included by ctest while it reads its test list: lists the test cases of one
# Catch2 executable and registers each as its own test (#217). The variables
# come from the file logsquirl_add_catch_tests() generated.

# ctest reads this with `cmake -P`, so no policy of the project reaches it and it
# has to set the two it relies on itself (#453):
#
#   CMP0007 -- `list()` keeps the empty elements of the listing instead of
#   dropping them silently. The empty lines are removed below by the filter that
#   names them, so nothing else goes with them and the test list is the same.
#   CMP0011 -- while this one is unset, setting a policy in an included script is
#   a developer warning of its own.
cmake_policy(SET CMP0011 NEW)
cmake_policy(SET CMP0007 NEW)

set(_logsquirl_catch_failed "")

if(NOT EXISTS "${_logsquirl_catch_executable}")
    set(_logsquirl_catch_failed "the test executable ${_logsquirl_catch_executable} is not built")
else()
    execute_process(
        COMMAND "${_logsquirl_catch_executable}" ${_logsquirl_catch_extra_args} --list-test-names-only
        WORKING_DIRECTORY "${_logsquirl_catch_working_dir}"
        OUTPUT_VARIABLE _logsquirl_catch_output
        ERROR_VARIABLE _logsquirl_catch_error
        RESULT_VARIABLE _logsquirl_catch_result
    )

    # Test case names may hold characters CMake lists treat specially; park
    # them before splitting the output into lines.
    string(REPLACE "\r" "" _logsquirl_catch_output "${_logsquirl_catch_output}")
    string(REPLACE ";" "<LOGSQUIRL_SEMICOLON>" _logsquirl_catch_output "${_logsquirl_catch_output}")
    string(REPLACE "[" "<LOGSQUIRL_OPEN_BRACKET>" _logsquirl_catch_output "${_logsquirl_catch_output}")
    string(REPLACE "]" "<LOGSQUIRL_CLOSE_BRACKET>" _logsquirl_catch_output "${_logsquirl_catch_output}")
    string(REPLACE "\n" ";" _logsquirl_catch_lines "${_logsquirl_catch_output}")
    list(FILTER _logsquirl_catch_lines EXCLUDE REGEX "^$")
    # The test runners log to stdout too, in the pattern logging::enableLogging()
    # sets ("<ISO time> <type> [<thread>] ...").
    list(FILTER _logsquirl_catch_lines EXCLUDE REGEX
         "^[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]T[0-9][0-9]:[0-9][0-9]:[0-9][0-9][.0-9]* [a-z]+ <LOGSQUIRL_OPEN_BRACKET>")
    list(LENGTH _logsquirl_catch_lines _logsquirl_catch_count)

    # Catch exits with the number of listed test cases, truncated to 8 bits on
    # Unix. Anything else (a crash, a missing DLL, a failed QApplication, output
    # that is neither a name nor a log line) means the list is wrong, and a
    # silently shorter test list must not pass.
    math(EXPR _logsquirl_catch_count_8bit "${_logsquirl_catch_count} % 256")
    if(NOT _logsquirl_catch_result MATCHES "^-?[0-9]+$"
       OR NOT (_logsquirl_catch_result EQUAL _logsquirl_catch_count
               OR _logsquirl_catch_result EQUAL _logsquirl_catch_count_8bit))
        set(_logsquirl_catch_failed
            "listing the test cases exited with '${_logsquirl_catch_result}' after ${_logsquirl_catch_count} names: ${_logsquirl_catch_error}")
    elseif(_logsquirl_catch_count EQUAL 0)
        set(_logsquirl_catch_failed "the test executable lists no test cases")
    endif()
endif()

if(_logsquirl_catch_failed)
    # A test that fails with the reason as its output, so the failure shows up
    # by name in ctest's results and the JUnit report.
    set(_logsquirl_catch_test "${_logsquirl_catch_target}: test case discovery failed")
    add_test("${_logsquirl_catch_test}" "${_logsquirl_catch_cmake}" -E echo "${_logsquirl_catch_failed}")
    set_tests_properties("${_logsquirl_catch_test}" PROPERTIES WILL_FAIL TRUE)
    return()
endif()

foreach(_logsquirl_catch_name IN LISTS _logsquirl_catch_lines)
    string(REPLACE "<LOGSQUIRL_SEMICOLON>" ";" _logsquirl_catch_name "${_logsquirl_catch_name}")
    string(REPLACE "<LOGSQUIRL_OPEN_BRACKET>" "[" _logsquirl_catch_name "${_logsquirl_catch_name}")
    string(REPLACE "<LOGSQUIRL_CLOSE_BRACKET>" "]" _logsquirl_catch_name "${_logsquirl_catch_name}")
    # Catch quotes names that start with '#' in its listing.
    if(_logsquirl_catch_name MATCHES "^\"(#.*)\"$")
        set(_logsquirl_catch_name "${CMAKE_MATCH_1}")
    endif()

    # A test spec treats these characters as syntax; a backslash makes them
    # part of the name.
    set(_logsquirl_catch_spec "${_logsquirl_catch_name}")
    foreach(_logsquirl_catch_char "\\" "," "[" "]" "~" "\"")
        string(REPLACE "${_logsquirl_catch_char}" "\\${_logsquirl_catch_char}"
               _logsquirl_catch_spec "${_logsquirl_catch_spec}")
    endforeach()

    # A test spec reaches the executable through the command line, which
    # Windows re-encodes in the ANSI code page: a name outside printable ASCII
    # then matches nothing. Fail discovery by name instead (#217).
    if(_logsquirl_catch_name MATCHES "[^ -~]")
        set(_logsquirl_catch_test "${_logsquirl_catch_target}: test case discovery failed")
        add_test("${_logsquirl_catch_test}" "${_logsquirl_catch_cmake}" -E echo
            "test case name is not printable ASCII: ${_logsquirl_catch_name}")
        set_tests_properties("${_logsquirl_catch_test}" PROPERTIES WILL_FAIL TRUE)
        continue()
    endif()

    set(_logsquirl_catch_test "${_logsquirl_catch_target}: ${_logsquirl_catch_name}")
    # --warn NoTests: a spec that matches no test case fails instead of passing
    # with zero assertions.
    #
    # The case runs through CatchTestDiscoveryRunTest.cmake, which puts it
    # beside a settings file of its own: the test binaries share the directory
    # they are built into, and with it the settings file beside them (#370).
    add_test("${_logsquirl_catch_test}"
        "${_logsquirl_catch_cmake}" "-DTEST_BINARY=${_logsquirl_catch_executable}"
        -P "${_logsquirl_catch_run_script}"
        -- "${_logsquirl_catch_spec}" --warn NoTests ${_logsquirl_catch_extra_args})
    set_tests_properties("${_logsquirl_catch_test}" PROPERTIES
        TIMEOUT "${_logsquirl_catch_timeout}"
        WORKING_DIRECTORY "${_logsquirl_catch_working_dir}")
endforeach()
