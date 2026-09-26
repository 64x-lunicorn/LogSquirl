# Sorts the ThreadSanitizer output of one test case (#482).
#
#   logsquirl_tsan_filter(SUPPRESSIONS <tsan.supp> LOGS <log file>...
#                         OUTPUT_FILE <file> FAILURES <var> LEFT_OUT <var>)
#
# The ctest runner has TSan write its reports to log files instead of stderr
# and hands them here. A data race report is left out when both racing
# accesses were made inside a library listed in the suppression file with a
# "#@uninstrumented <library>" line: a library not built with TSan, whose own
# synchronization TSan cannot see (see cmake/tsan.supp for the libraries and
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
# over; the next frame names the module that made it. An atomic operation on
# the reference count of a Qt payload (QArrayData::ref/deref, inlined anywhere)
# is QtCore's: the payload is freed inside QtCore after a decrement TSan does
# not see there. An access whose frames say nothing counts as LogSquirl's.
#
# One race with an access in LogSquirl's code is left out as well: a slot that
# Qt calls for a queued signal (QObject::event in QtCore below it) reading an
# argument QtCore copied for that call (QMetaType::create in QtCore allocating
# it on the emitting thread). Qt hands the copy over through its event queue,
# and nothing else holds it.
#
# And so is its twin for a functor handed to QMetaObject::invokeMethod with a
# queued connection (#517): Qt's header code, inlined into LogSquirl's binary,
# allocates a QCallableObject and copies the functor into it on the calling
# thread (QMetaObject::invokeMethodCallableHelper<F> below that write), and
# QtCore calls or destroys that same QCallableObject<F> on the receiving thread
# (QObject::event or ~QQueuedMetaCallEvent in QtCore right below its impl).
# Where the receiving side names its functor type, it must be F; a race
# against any other code still fails.

# Who made the access these frames (innermost first) describe:
#   out_var            the library, or "" when it was not one of them
#   out_var_ARGUMENT   TRUE when it is QtCore allocating a queued argument
#   out_var_IN_EVENT   TRUE when it runs in a slot QtCore called for an event
function(_logsquirl_tsan_access_owner out_var atomic libraries)
  set(_owner "")
  set(_reference_count FALSE)
  set(_argument FALSE)
  set(_in_event FALSE)
  set(_first TRUE)
  set(_decided FALSE)
  foreach(_frame IN LISTS ARGN)
    if(_frame MATCHES "QObject::event\\(QEvent\\*\\) .*\\(libQt6Core\\.so\\.6\\+0x")
      set(_in_event TRUE)
    endif()
    if(_decided OR _frame MATCHES "\\(libtsan\\.so")
      continue()
    endif()
    if(_first AND _frame MATCHES "QMetaType::create\\(.*\\(libQt6Core\\.so\\.6\\+0x"
       AND "libQt6Core.so.6" IN_LIST libraries)
      set(_argument TRUE)
    endif()
    set(_first FALSE)
    if(_frame MATCHES "/include/Qt[A-Za-z0-9]*/" OR _frame MATCHES "/include/c\\+\\+/")
      if(atomic AND _frame MATCHES "QArrayData::(ref|deref)\\(")
        set(_reference_count TRUE)
      endif()
      continue()
    endif()
    if(_reference_count)
      if("libQt6Core.so.6" IN_LIST libraries)
        set(_owner "libQt6Core.so.6")
      endif()
    elseif(_frame MATCHES "\\(([^ ()]+)\\+0x[0-9a-f]+\\)")
      if(CMAKE_MATCH_1 IN_LIST libraries)
        set(_owner "${CMAKE_MATCH_1}")
      endif()
    endif()
    set(_decided TRUE)
  endforeach()
  set(${out_var} "${_owner}" PARENT_SCOPE)
  set(${out_var}_ARGUMENT "${_argument}" PARENT_SCOPE)
  set(${out_var}_IN_EVENT "${_in_event}" PARENT_SCOPE)
endfunction()

# Whether a write (write_frames, innermost first) was made while Qt built a
# queued call of a functor, and the other access (other_frames) is QtCore
# delivering or destroying such a call (#517): QObject::event or
# ~QQueuedMetaCallEvent in QtCore right below the QCallableObject's impl. TSan
# names inlined frames in two forms, "QMetaObject::invokeMethodCallableHelper<F>
# (QtPrivate::ContextTypeForFunctor<...>)" / "QtPrivate::QCallableObject<F,
# ...>::impl(...)" or just "invokeMethodCallableHelper<F>" / "impl"; where the
# receiving side names its functor type, it must be F.
function(_logsquirl_tsan_queued_functor out_var libraries write_frames other_frames)
  set(${out_var} FALSE PARENT_SCOPE)
  if(NOT "libQt6Core.so.6" IN_LIST libraries)
    return()
  endif()
  set(_functor "")
  foreach(_frame IN LISTS write_frames)
    if(_frame MATCHES "invokeMethodCallableHelper<(.*)>\\(QtPrivate::ContextTypeForFunctor<")
      set(_functor "${CMAKE_MATCH_1}")
      break()
    elseif(_frame MATCHES "invokeMethodCallableHelper<(.*[^ ]) ?> /")
      set(_functor "${CMAKE_MATCH_1}")
      break()
    endif()
  endforeach()
  if(_functor STREQUAL "")
    return()
  endif()
  set(_impl "")
  foreach(_frame IN LISTS other_frames)
    if(_frame MATCHES "(QObject::event\\(QEvent\\*\\)|QQueuedMetaCallEvent::~QQueuedMetaCallEvent\\(\\)) .*\\(libQt6Core\\.so\\.6\\+0x")
      break()
    endif()
    set(_impl "${_frame}")
  endforeach()
  if(NOT _impl MATCHES "^    #[0-9]+ (QtPrivate::QCallableObject<.*>::)?impl[( ].*/include/QtCore/qobjectdefs_impl\\.h:")
    return()
  endif()
  if(_impl MATCHES "QtPrivate::QCallableObject<")
    string(FIND "${_impl}" "QtPrivate::QCallableObject<${_functor}, QtPrivate::List<" _call)
    if(_call EQUAL -1)
      return()
    endif()
  endif()
  set(${out_var} TRUE PARENT_SCOPE)
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
  set(_atomic FALSE)
  foreach(_line IN LISTS ARGN)
    if(_in_access AND _line MATCHES "^    #[0-9]+ ")
      list(APPEND _frames "${_line}")
      continue()
    endif()
    if(_in_access)
      _logsquirl_tsan_access_owner(_owner_${_accesses} "${_atomic}" "${libraries}" ${_frames})
      set(_frames_${_accesses} "${_frames}")
      set(_in_access FALSE)
    endif()
    if(_accesses LESS 2 AND _line MATCHES "^  (Previous )?([Aa]tomic )?([Ww]rite|[Rr]ead) of size [0-9]+ at ")
      math(EXPR _accesses "${_accesses} + 1")
      set(_in_access TRUE)
      set(_frames "")
      if(_line MATCHES "[Aa]tomic ")
        set(_atomic TRUE)
      else()
        set(_atomic FALSE)
      endif()
      if(_line MATCHES "^  (Previous )?([Aa]tomic )?[Ww]rite ")
        set(_write_${_accesses} TRUE)
      else()
        set(_write_${_accesses} FALSE)
      endif()
    endif()
  endforeach()
  if(_in_access)
    _logsquirl_tsan_access_owner(_owner_${_accesses} "${_atomic}" "${libraries}" ${_frames})
    set(_frames_${_accesses} "${_frames}")
  endif()

  if(_accesses EQUAL 2)
    if(NOT _owner_1 STREQUAL "" AND NOT _owner_2 STREQUAL "")
      set(_result "${_owner_1} and ${_owner_2}")
    elseif((_owner_1_ARGUMENT AND _owner_2_IN_EVENT) OR (_owner_2_ARGUMENT AND _owner_1_IN_EVENT))
      set(_result "a queued call's argument copied by libQt6Core.so.6 and its slot")
    else()
      set(_functor FALSE)
      if(_write_1)
        _logsquirl_tsan_queued_functor(_functor "${libraries}" "${_frames_1}" "${_frames_2}")
      endif()
      if(NOT _functor AND _write_2)
        _logsquirl_tsan_queued_functor(_functor "${libraries}" "${_frames_2}" "${_frames_1}")
      endif()
      if(_functor)
        set(_result "a queued call's functor copied by Qt and its call in libQt6Core.so.6")
      endif()
    endif()
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
