# Runs the command line tool on a temporary Log File, as a user would, and
# checks what it prints and the code it exits with (#247): the Log Lines the
# Search matched on stdout, in order, and nothing else; a failure the engine
# reports and every log message it writes described on stderr, a failure with
# a non-zero exit code (#327).
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

# A Log File is read in the Encoding it is detected as, and its matching Log
# Lines are printed as they are in the file, byte for byte (#326).
set(_utf8_file "${WORK_DIR}/grep-utf8.log")
set(_utf8_expected "hit: Grüße aus München\nhit: Öl bei 42 °C, Größe 5 µm\n")
file(WRITE "${_utf8_file}" "hit: Grüße aus München\nmiss: nothing to see\nhit: Öl bei 42 °C, Größe 5 µm\n")
run_grep("${_utf8_file}" -e "^hit")
if(NOT _result STREQUAL "0")
  fail("a Search in a UTF-8 Log File exits with 0")
endif()
if(NOT _stdout STREQUAL _utf8_expected)
  fail("a Search prints the UTF-8 Log Lines it matched byte for byte")
endif()

# The Search matches the Log Lines in that Encoding too, so a pattern with
# non-ASCII text finds them.
run_grep("${_utf8_file}" -e "München")
if(NOT _result STREQUAL "0")
  fail("a Search for non-ASCII text exits with 0")
endif()
if(NOT (_stdout STREQUAL "hit: Grüße aus München\n"))
  fail("a Search for non-ASCII text prints the Log Line it matched")
endif()

# A Latin-1 Log File prints as UTF-8 text. Its bytes are built one by one, so
# this script's own Encoding does not decide what is written: ä ö ü ß, which
# every Latin Encoding the detection may land on decodes the same way.
string(ASCII 228 _l1_ae)
string(ASCII 246 _l1_oe)
string(ASCII 252 _l1_ue)
string(ASCII 223 _l1_sz)
set(_latin1_file "${WORK_DIR}/grep-latin1.log")
file(WRITE "${_latin1_file}"
     "hit: Gr${_l1_oe}${_l1_sz}e und M${_l1_ue}nchen, ${_l1_ae}rger mit T${_l1_ue}ren\n"
     "miss: nothing to see\n"
     "hit: sch${_l1_oe}ne gr${_l1_ue}${_l1_sz}e, ${_l1_ae}u${_l1_sz}ere W${_l1_ae}rme\n")
run_grep("${_latin1_file}" -e "^hit")
if(NOT _result STREQUAL "0")
  fail("a Search in a Latin-1 Log File exits with 0")
endif()
if(NOT (_stdout STREQUAL "hit: Größe und München, ärger mit Türen\nhit: schöne grüße, äußere Wärme\n"))
  fail("a Search prints the Log Lines of a Latin-1 Log File as UTF-8 text")
endif()

# A Log File without a trailing line feed is warned about, and the warning is
# a diagnostic: it goes to stderr, so stdout carries the matching Log Lines
# and nothing else and can be piped into another tool (#327).
set(_no_lf_file "${WORK_DIR}/grep-no-lf.log")
file(WRITE "${_no_lf_file}" "first fizz\nno match here\nlast fizz")
run_grep("${_no_lf_file}" -e fizz)
if(NOT _result STREQUAL "0")
  fail("a Search in a Log File without a trailing line feed exits with 0")
endif()
if(NOT (_stdout STREQUAL "first fizz\nlast fizz\n"))
  fail("a Search in a Log File without a trailing line feed prints only its matches")
endif()
if(NOT (_stderr MATCHES "Non LF terminated file"))
  fail("the warning about a missing trailing line feed is on stderr")
endif()
# A log message carries the function it was logged from, as "@<line>]".
if(_stdout MATCHES "@[0-9]+\\]")
  fail("no log message is printed on stdout")
endif()

# Debug output is a diagnostic like any other: asking for it writes log
# messages on stderr and changes neither the matches on stdout nor the exit
# code (#345). The tool has no font database, so what the settings log about
# the Main Font must not reach for one.
run_grep("${_log_file}" -e fizz -d 4)
if(NOT _result STREQUAL "0")
  fail("a Search asked for debug output exits with 0")
endif()
if(NOT _stdout STREQUAL _expected)
  fail("a Search asked for debug output prints the same matching Log Lines")
endif()
if(NOT (_stderr MATCHES "@[0-9]+\\]"))
  fail("a Search asked for debug output writes its log messages on stderr")
endif()
if(NOT (_stderr MATCHES "Main font is [^\n]+"))
  fail("a Search asked for debug output logs the Main Font without a font database")
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
