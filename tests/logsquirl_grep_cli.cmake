# Runs the command line tool on a temporary Log File, as a user would, and
# checks what it prints and the code it exits with (#247): the Log Lines the
# Search matched on stdout, in order; a failure the engine reports described
# on stderr, with a non-zero exit code.
#
# Usage: cmake -DGREP=<path to logsquirl_grep> -DWORK_DIR=<scratch directory>
#              -P logsquirl_grep_cli.cmake

cmake_minimum_required(VERSION 3.16)

if(NOT GREP OR NOT EXISTS "${GREP}")
  message(FATAL_ERROR "GREP is not the command line tool: '${GREP}'")
endif()
if(NOT WORK_DIR)
  message(FATAL_ERROR "WORK_DIR is not set")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

set(_failures "")

# Runs the tool with the given arguments; sets _stdout, _stderr and _result.
# Line endings are normalized, so the checks hold on every platform.
function(run_grep)
  execute_process(
    COMMAND "${GREP}" ${ARGN}
    WORKING_DIRECTORY "${WORK_DIR}"
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
    RESULT_VARIABLE _res
    TIMEOUT 60
  )
  string(REPLACE "\r\n" "\n" _out "${_out}")
  string(REPLACE "\r\n" "\n" _err "${_err}")
  set(_stdout "${_out}" PARENT_SCOPE)
  set(_stderr "${_err}" PARENT_SCOPE)
  set(_result "${_res}" PARENT_SCOPE)
endfunction()

# Records a failed check, with what the tool did.
macro(fail _description)
  list(APPEND _failures "${_description}\n    exit: ${_result}\n    stdout: ${_stdout}\n    stderr: ${_stderr}")
endmacro()

# A Log File of 2500 Log Lines; every other one says "fizz", so the matches
# are printed in more than one chunk.
set(_log_file "${WORK_DIR}/grep.log")
set(_content "")
set(_expected "")
foreach(_number RANGE 0 2499)
  math(EXPR _odd "${_number} % 2")
  if(_odd)
    string(APPEND _content "line ${_number} buzz\n")
  else()
    string(APPEND _content "line ${_number} fizz\n")
    string(APPEND _expected "line ${_number} fizz\n")
  endif()
endforeach()
file(WRITE "${_log_file}" "${_content}")

# The Log Lines that match are printed, in order, and nothing else.
run_grep("${_log_file}" -e fizz)
if(NOT _result STREQUAL "0")
  fail("a Search that matches exits with 0")
endif()
if(NOT _stdout STREQUAL _expected)
  string(LENGTH "${_stdout}" _got_length)
  string(LENGTH "${_expected}" _expected_length)
  list(APPEND _failures
       "a Search prints the matching Log Lines in order (got ${_got_length} characters, "
       "expected ${_expected_length})")
endif()

# A regular expression is matched as one.
run_grep("${_log_file}" -e "^line 1[0-9] f")
if(NOT _result STREQUAL "0")
  fail("a regular expression Search exits with 0")
endif()
if(NOT (_stdout STREQUAL "line 10 fizz\nline 12 fizz\nline 14 fizz\nline 16 fizz\nline 18 fizz\n"))
  fail("a regular expression Search prints the Log Lines it matched")
endif()

# Matching Log Lines are printed as they are in the file: tabs kept, a
# carriage return before the line feed dropped, the last Log Line too.
set(_crlf_file "${WORK_DIR}/grep-crlf.log")
file(WRITE "${_crlf_file}"
     "first fizz\r\nno match here\r\n\tcolumn\tfizz\nbuzz\nfizz at the end\r\n")
run_grep("${_crlf_file}" -e fizz)
if(NOT _result STREQUAL "0")
  fail("a Search in a Log File with carriage returns exits with 0")
endif()
if(NOT (_stdout STREQUAL "first fizz\n\tcolumn\tfizz\nfizz at the end\n"))
  fail("a Search prints its Log Lines without their carriage returns")
endif()

# No match is no failure.
run_grep("${_log_file}" -e "no such text")
if(NOT _result STREQUAL "0")
  fail("a Search that matches nothing exits with 0")
endif()
if(NOT _stdout STREQUAL "")
  fail("a Search that matches nothing prints nothing")
endif()

# An invalid pattern is a failure the engine reports: described on stderr.
run_grep("${_log_file}" -e "(")
if(_result STREQUAL "0")
  fail("an invalid pattern exits non-zero")
endif()
if(NOT (_stderr MATCHES "logsquirl_grep: [^\n]+"))
  fail("an invalid pattern is described on stderr")
endif()
if(_stdout MATCHES "fizz")
  fail("an invalid pattern prints no Log Line")
endif()

# Without a Log File there is nothing to search.
run_grep(-e fizz)
if(_result STREQUAL "0")
  fail("no Log File exits non-zero")
endif()
if(NOT (_stderr MATCHES "logsquirl_grep: [^\n]+"))
  fail("no Log File is described on stderr")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")

if(_failures)
  list(JOIN _failures "\n  " _report)
  message(FATAL_ERROR "logsquirl_grep:\n  ${_report}")
endif()

message(STATUS "logsquirl_grep printed its matches and reported its failures")
