# Sorts the ThreadSanitizer output of one test case (#482, #510).
#
#   logsquirl_tsan_filter(SUPPRESSIONS <tsan.supp> LOGS <log file>...
#                         OUTPUT_FILE <file> FAILURES <var> LEFT_OUT <var>)
#
# The ctest runner has TSan write its reports to log files instead of stderr
# and hands them here. A data race report is left out when both racing
# accesses were made inside a library listed in the suppression file with a
# "#@uninstrumented <library>" line: a library not built with TSan, whose own
# synchronization TSan cannot see (see cmake/tsan.supp for the libraries, only
# glibc since Qt is built with TSan, and
# docs/adr/0007-tsan-suppresses-onetbb-and-uninstrumented-qt-internals.md for
# why this is not a `race:` suppression). Everything else counts:
#
#   OUTPUT_FILE  written with what to print: the logs without the reports left
#                out, and a line that counts those
#   FAILURES     the number of reports kept, plus one for any other output of
#                TSan than its reports and its summary lines (a TSan error, a
#                warning about a thread): 0 when the case passes as far as TSan
#                goes
#   LEFT_OUT     the number of reports left out
#
# Who made an access: TSan's own interceptor frame (operator new, free, memcpy
# ...) and the frames of inlined Qt and C++ standard library headers are passed
# over; the next frame names the module that made it. An access whose frames
# say nothing counts as LogSquirl's.
#
# Until Qt was built with TSan (#510), two more kinds of report were left out
# here: an inlined QArrayData::ref/deref counted as QtCore's, and a queued slot
# reading an argument QMetaType::create had copied. Both hid what an
# instrumented Qt now shows.

# Who made the access these frames (innermost first) describe: the library, or
# "" when it was not one of them.
function(_logsquirl_tsan_access_owner out_var libraries)
  set(_owner "")
  foreach(_frame IN LISTS ARGN)
    if(_frame MATCHES "\\(libtsan\\.so")
      continue()
    endif()
    if(_frame MATCHES "/include/Qt[A-Za-z0-9]*/" OR _frame MATCHES "/include/c\\+\\+/")
      continue()
    endif()
    if(_frame MATCHES "\\(([^ ()]+)\\+0x[0-9a-f]+\\)")
      if(CMAKE_MATCH_1 IN_LIST libraries)
        set(_owner "${CMAKE_MATCH_1}")
      endif()
    endif()
    break()
  endforeach()
  set(${out_var} "${_owner}" PARENT_SCOPE)
endfunction()

# Whether a report (its lines between the two "=====" lines) is left out;
# out_var then says why, and is empty otherwise.
function(_logsquirl_tsan_report_left_out out_var libraries)
  set(_result "")
  if(ARGC LESS 3)
    set(${out_var} "" PARENT_SCOPE)
    return()
  endif()
  list(GET ARGN 0 _warning)
  if(NOT _warning MATCHES "^WARNING: ThreadSanitizer: data race")
    set(${out_var} "" PARENT_SCOPE)
    return()
  endif()
  set(_accesses 0)
  set(_in_access FALSE)
  set(_frames "")
  foreach(_line IN LISTS ARGN)
    if(_in_access AND _line MATCHES "^    #[0-9]+ ")
      list(APPEND _frames "${_line}")
      continue()
    endif()
    if(_in_access)
      _logsquirl_tsan_access_owner(_owner_${_accesses} "${libraries}" ${_frames})
      set(_in_access FALSE)
    endif()
    if(_accesses LESS 2 AND _line MATCHES "^  (Previous )?([Aa]tomic )?([Ww]rite|[Rr]ead) of size [0-9]+ at ")
      math(EXPR _accesses "${_accesses} + 1")
      set(_in_access TRUE)
      set(_frames "")
    endif()
  endforeach()
  if(_in_access)
    _logsquirl_tsan_access_owner(_owner_${_accesses} "${libraries}" ${_frames})
  endif()

  if(_accesses EQUAL 2 AND NOT _owner_1 STREQUAL "" AND NOT _owner_2 STREQUAL "")
    set(_result "${_owner_1} and ${_owner_2}")
  endif()
  set(${out_var} "${_result}" PARENT_SCOPE)
endfunction()

# Appends a line (or several) to the output file, the parked characters back.
function(_logsquirl_tsan_write file text)
  string(REPLACE "<TSAN_CLOSE_BRACKET>" "]" text "${text}")
  string(REPLACE "<TSAN_OPEN_BRACKET>" "[" text "${text}")
  string(REPLACE "<TSAN_SEMICOLON>" ";" text "${text}")
  string(REPLACE "<TSAN_BACKSLASH>" "\\" text "${text}")
  file(APPEND "${file}" "${text}\n")
endfunction()

function(logsquirl_tsan_filter)
  cmake_parse_arguments(PARSE_ARGV 0 arg "" "SUPPRESSIONS;OUTPUT_FILE;FAILURES;LEFT_OUT" "LOGS")

  file(STRINGS "${arg_SUPPRESSIONS}" _declared REGEX "^#@uninstrumented ")
  set(_libraries "")
  foreach(_line IN LISTS _declared)
    string(REGEX REPLACE "^#@uninstrumented +([^ ]+).*$" "\\1" _library "${_line}")
    list(APPEND _libraries "${_library}")
  endforeach()

  # What is printed goes to the file as it is found: a case can print thousands
  # of report lines, and a CMake variable is copied whole on every append.
  file(WRITE "${arg_OUTPUT_FILE}" "")
  set(_failures 0)
  set(_left_out 0)
  set(_left_out_pairs "")
  foreach(_log IN LISTS arg_LOGS)
    file(READ "${_log}" _content)
    # Frames hold characters a CMake list treats as syntax; park them.
    string(REPLACE "\\" "<TSAN_BACKSLASH>" _content "${_content}")
    string(REPLACE ";" "<TSAN_SEMICOLON>" _content "${_content}")
    string(REPLACE "[" "<TSAN_OPEN_BRACKET>" _content "${_content}")
    string(REPLACE "]" "<TSAN_CLOSE_BRACKET>" _content "${_content}")
    string(REPLACE "\n" ";" _lines "${_content}")
    unset(_content)

    set(_in_report FALSE)
    set(_in_matched FALSE)
    set(_report "")
    foreach(_line IN LISTS _lines)
      if(_in_report)
        if(_line STREQUAL "==================")
          _logsquirl_tsan_report_left_out(_reason "${_libraries}" ${_report})
          if(_reason STREQUAL "")
            math(EXPR _failures "${_failures} + 1")
            list(JOIN _report "\n" _text)
            _logsquirl_tsan_write("${arg_OUTPUT_FILE}" "==================\n${_text}\n==================")
          else()
            math(EXPR _left_out "${_left_out} + 1")
            list(APPEND _left_out_pairs "${_reason}")
          endif()
          set(_in_report FALSE)
          set(_report "")
        else()
          list(APPEND _report "${_line}")
        endif()
      elseif(_line STREQUAL "==================")
        set(_in_report TRUE)
        set(_in_matched FALSE)
      elseif(_line STREQUAL "")
        continue()
      elseif(_line MATCHES "^ThreadSanitizer: reported [0-9]+ warnings$")
        continue()
      elseif(_line MATCHES "^ThreadSanitizer: Matched [0-9]+ suppressions")
        set(_in_matched TRUE)
        _logsquirl_tsan_write("${arg_OUTPUT_FILE}" "${_line}")
      elseif(_in_matched AND _line MATCHES "^[0-9]+ [a-z_]+:")
        _logsquirl_tsan_write("${arg_OUTPUT_FILE}" "${_line}")
      else()
        # Not a report TSan recovers from: a TSan error, a warning about a
        # thread, a report cut off. It fails the case, printed as it came.
        math(EXPR _failures "${_failures} + 1")
        _logsquirl_tsan_write("${arg_OUTPUT_FILE}" "${_line}")
      endif()
    endforeach()
    if(_in_report)
      math(EXPR _failures "${_failures} + 1")
      list(JOIN _report "\n" _text)
      _logsquirl_tsan_write("${arg_OUTPUT_FILE}" "==================\n${_text}")
    endif()
  endforeach()

  set(_pairs ${_left_out_pairs})
  list(REMOVE_DUPLICATES _pairs)
  foreach(_pair IN LISTS _pairs)
    set(_count 0)
    foreach(_each IN LISTS _left_out_pairs)
      if(_each STREQUAL _pair)
        math(EXPR _count "${_count} + 1")
      endif()
    endforeach()
    _logsquirl_tsan_write("${arg_OUTPUT_FILE}"
      "ThreadSanitizer: left out ${_count} data race(s) between ${_pair} (cmake/tsan.supp)")
  endforeach()

  set(${arg_FAILURES} "${_failures}" PARENT_SCOPE)
  set(${arg_LEFT_OUT} "${_left_out}" PARENT_SCOPE)
endfunction()

# --- For a test script that runs a LogSquirl binary itself (#482) ------------
#
# The ctest runner (cmake/CatchTestDiscoveryRunTest.cmake) sorts the reports of
# every Catch2 case. A script that starts a LogSquirl binary on its own (the
# command line tool, the application, a test binary) does the same with these:
#
#   logsquirl_tsan_prepare(<directory>)
#       sets TSAN_OPTIONS for what the script starts next: the suppression
#       file, reports written to files in <directory>, and exitcode=0 so the
#       program keeps its own exit code
#   logsquirl_tsan_check(<directory> <failures var> <output var>)
#       sorts what TSan wrote there since, as the runner does, and empties the
#       directory: <failures var> is 0 when nothing is left that fails a case,
#       <output var> what to print
#
# Outside a TSan build TSAN_OPTIONS is unread and the directory stays empty.
set(_LOGSQUIRL_TSAN_SUPPRESSIONS "${CMAKE_CURRENT_LIST_DIR}/tsan.supp")

function(logsquirl_tsan_prepare directory)
  file(REMOVE_RECURSE "${directory}")
  file(MAKE_DIRECTORY "${directory}")
  set(ENV{TSAN_OPTIONS}
      "suppressions=${_LOGSQUIRL_TSAN_SUPPRESSIONS}:log_path=${directory}/tsan:exitcode=0")
endfunction()

function(logsquirl_tsan_check directory failures_var output_var)
  file(GLOB _logs "${directory}/tsan.*")
  set(_failures 0)
  set(_output "")
  if(_logs)
    logsquirl_tsan_filter(
      SUPPRESSIONS "${_LOGSQUIRL_TSAN_SUPPRESSIONS}"
      LOGS ${_logs}
      OUTPUT_FILE "${directory}/sorted.txt"
      FAILURES _failures
      LEFT_OUT _left_out)
    file(READ "${directory}/sorted.txt" _output)
    file(REMOVE ${_logs} "${directory}/sorted.txt")
  endif()
  set(${failures_var} "${_failures}" PARENT_SCOPE)
  set(${output_var} "${_output}" PARENT_SCOPE)
endfunction()
