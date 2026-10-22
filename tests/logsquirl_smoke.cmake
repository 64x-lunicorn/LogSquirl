# Starts the application and checks that it runs and prints its version, the
# way `logsquirl_smoke` always did -- but with its own home, temporary
# directory and QStandardPaths locations, all inside WORK_DIR (#328). Running
# ctest on a developer machine may not read or change that developer's own
# settings, Session, cache, Log Formats or plugins.
#
# The locations per platform, matching tests/e2e/isolated_instance.py:
#   - HOME and TMPDIR everywhere: the single-instance lock file and the local
#     socket live in the temporary directory.
#   - Linux: the XDG_* directories decide every QStandardPaths location.
#   - macOS: Core Foundation resolves them and reads the home directory from
#     the password database, so HOME alone moves nothing; CFFIXED_USER_HOME is
#     the override it does honour. Qt's QStandardPaths test mode is no help,
#     Qt 6.11 ignores it there.
#   - Windows: QStandardPaths follows the known folders, which no environment
#     variable moves. USERPROFILE and the temporary directory are set for what
#     does follow them; printing the version reads no settings anyway.
#
# Usage: cmake -DLOGSQUIRL=<path to the executable> -DWORK_DIR=<scratch directory>
#              -P logsquirl_smoke.cmake

cmake_minimum_required(VERSION 3.16)

if(NOT LOGSQUIRL OR NOT EXISTS "${LOGSQUIRL}")
  message(FATAL_ERROR "LOGSQUIRL is not the application: '${LOGSQUIRL}'")
endif()
if(NOT WORK_DIR)
  message(FATAL_ERROR "WORK_DIR is not set")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
set(_home "${WORK_DIR}/home")
set(_tmp "${WORK_DIR}/tmp")
file(MAKE_DIRECTORY "${_home}" "${_tmp}")

# The single-instance lock and socket carry this id in their names, so the
# check below finds what this run left behind and nothing else (#844).
string(RANDOM LENGTH 8 ALPHABET "abcdefghijklmnopqrstuvwxyz0123456789" _instance_id)
set(_instance_id "smoke-${_instance_id}")

set(_environment
    "HOME=${_home}"
    "TMPDIR=${_tmp}"
    "QT_QPA_PLATFORM=offscreen"
    "LOGSQUIRL_INSTANCE_ID=${_instance_id}"
)

if(APPLE)
  list(APPEND _environment "CFFIXED_USER_HOME=${_home}")
elseif(WIN32)
  list(APPEND _environment "USERPROFILE=${_home}" "TEMP=${_tmp}" "TMP=${_tmp}")
else()
  list(APPEND _environment
       "XDG_CONFIG_HOME=${_home}/.config"
       "XDG_CACHE_HOME=${_home}/.cache"
       "XDG_DATA_HOME=${_home}/.local/share"
  )
endif()

# A ThreadSanitizer build sorts the reports of what this starts the way the
# ctest runner does for every test case (#482); ignored by any other build.
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/TsanReportFilter.cmake")
set(_tsan_dir "${WORK_DIR}_tsan")
logsquirl_tsan_prepare("${_tsan_dir}")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_environment} -- "${LOGSQUIRL}" -platform offscreen -v
  WORKING_DIRECTORY "${WORK_DIR}"
  OUTPUT_VARIABLE _stdout
  ERROR_VARIABLE _stderr
  RESULT_VARIABLE _result
  TIMEOUT 120
)

# -v ends the application cleanly, so the single-instance lock file and socket
# go with it (#844). They live in an owner-only folder under the temporary
# directory, under /tmp when that path is too deep for a socket (#841), or
# under the runtime directory on Linux; a named pipe on Windows is no file.
if(NOT WIN32)
  set(_instance_patterns
      "${_tmp}/logsquirl-*/*${_instance_id}*"
      "/tmp/logsquirl-*/*${_instance_id}*"
  )
  if(DEFINED ENV{XDG_RUNTIME_DIR})
    list(APPEND _instance_patterns "$ENV{XDG_RUNTIME_DIR}/logsquirl-*/*${_instance_id}*")
  endif()
  file(GLOB _left_behind LIST_DIRECTORIES false ${_instance_patterns})
  if(_left_behind)
    file(REMOVE ${_left_behind})
    file(REMOVE_RECURSE "${WORK_DIR}")
    string(REPLACE ";" "\n  " _left_behind "${_left_behind}")
    message(FATAL_ERROR "logsquirl -v left its single-instance lock or socket behind:\n  ${_left_behind}")
  endif()
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
logsquirl_tsan_check("${_tsan_dir}" _tsan_failures _tsan_output)
if(NOT _tsan_failures EQUAL 0)
  message(FATAL_ERROR "ThreadSanitizer: ${_tsan_failures} finding(s) in logsquirl -v:\n${_tsan_output}")
endif()
file(REMOVE_RECURSE "${_tsan_dir}")

if(NOT _result STREQUAL "0")
  message(FATAL_ERROR "logsquirl -v exited with ${_result}\n"
                      "  stdout: ${_stdout}\n  stderr: ${_stderr}"
  )
endif()

string(TOLOWER "${_stdout}${_stderr}" _output)
if(NOT _output MATCHES "logsquirl")
  message(FATAL_ERROR "logsquirl -v printed no version\n"
                      "  stdout: ${_stdout}\n  stderr: ${_stderr}"
  )
endif()

message(STATUS "logsquirl started isolated and printed its version")
