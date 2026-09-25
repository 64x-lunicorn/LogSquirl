# The version a build calls itself is the one the project declares, spelled
# the way it is spelled -- a leading zero included (#372).
#
# There are two ways a build gets its version, and the checks below cover
# both, whichever way this build was made:
#   - no LOGSQUIRL_VERSION in the environment, which is every build a
#     developer makes: project() in CMakeLists.txt decides, and it keeps a
#     leading zero only from cmake_minimum_required(VERSION 3.16) on, where
#     policy CMP0096 defaults to NEW. Below 3.16 `VERSION 26.07.0` became
#     26.7.0, and the binaries said so while the release called itself
#     26.07.0.
#   - LOGSQUIRL_VERSION set, which is every build CI makes:
#     cmake/project_version.cmake rebuilds the version from that string and
#     the zero survives, because it works on the text.
#
# Only one of the two made the binary under test, so the other is exercised
# where it is decided: the declaration is configured as a project of its own,
# from the text of this repository's CMakeLists.txt, and the rebuild is run
# from this repository's own module. What the built tool reports is checked
# against whichever of the two made it.
#
# The tool is the command line one because it is the one a test can run
# anywhere; the desktop application takes its version from the same generated
# header.
#
# Usage: cmake -DGREP=<path to logsquirl_grep> -DPROJECT_DIR=<source directory>
#              -DVERSION_OVERRIDE=<BUILD_VERSION> -DVERSION_BUILD_NUMBER=<BUILD_NUMBER>
#              -DWORK_DIR=<scratch directory> -P version_as_declared.cmake

cmake_minimum_required(VERSION 3.16)

if(NOT GREP OR NOT EXISTS "${GREP}")
  message(FATAL_ERROR "GREP is not the command line tool: '${GREP}'")
endif()
if(NOT PROJECT_DIR OR NOT EXISTS "${PROJECT_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "PROJECT_DIR is not the project: '${PROJECT_DIR}'")
endif()
if(NOT WORK_DIR)
  message(FATAL_ERROR "WORK_DIR is not set")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

set(_failures "")

macro(fail _description)
  list(APPEND _failures "${_description}")
endmacro()

file(READ "${PROJECT_DIR}/CMakeLists.txt" _cmakelists)

# Everything up to and including the project() call, verbatim: the minimum
# this project asks for, whatever it does with policies afterwards, and the
# VERSION it declares. Configured as a project of its own it must produce the
# version as written -- this is the developer's build, the one no environment
# variable rescues, and the check holds even when this suite runs from a build
# that was handed a version.
string(FIND "${_cmakelists}" "\nproject(" _project_start)
if(_project_start EQUAL -1)
  message(FATAL_ERROR "CMakeLists.txt has no project() call on a line of its own")
endif()
string(SUBSTRING "${_cmakelists}" ${_project_start} -1 _from_project)
string(FIND "${_from_project}" "\n)" _project_end)
if(_project_end EQUAL -1)
  message(FATAL_ERROR "the project() call in CMakeLists.txt does not end on a line of its own")
endif()
math(EXPR _preamble_length "${_project_start} + ${_project_end} + 2")
string(SUBSTRING "${_cmakelists}" 0 ${_preamble_length} _preamble)
math(EXPR _project_length "${_project_end} + 2")
string(SUBSTRING "${_from_project}" 0 ${_project_length} _project_call)

# The declared version, read from the project() call rather than from
# PROJECT_VERSION: an expectation taken from PROJECT_VERSION would agree with
# a project() that dropped the zero, which is the defect itself.
if(NOT _project_call MATCHES "VERSION[ \t]+([0-9][0-9a-zA-Z.]*)")
  message(FATAL_ERROR "the project() call in CMakeLists.txt declares no VERSION")
endif()
set(_declared "${CMAKE_MATCH_1}")

# The harness needs no compiler, only the version; the languages this project
# builds would only slow the configure down.
string(REPLACE "LANGUAGES C CXX ASM" "LANGUAGES NONE" _harness_text "${_preamble}")
set(_harness "${WORK_DIR}/declaration")
file(MAKE_DIRECTORY "${_harness}")
file(WRITE "${_harness}/CMakeLists.txt"
     "${_harness_text}\nfile(WRITE \"\${CMAKE_BINARY_DIR}/version.txt\" \"\${PROJECT_VERSION}\")\n")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -S "${_harness}" -B "${_harness}/build"
  OUTPUT_VARIABLE _harness_stdout
  ERROR_VARIABLE _harness_stderr
  RESULT_VARIABLE _harness_result
  TIMEOUT 300
)
if(NOT _harness_result STREQUAL "0")
  message(FATAL_ERROR "the declaration of CMakeLists.txt does not configure\n"
                      "  stdout: ${_harness_stdout}\n  stderr: ${_harness_stderr}"
  )
endif()
file(READ "${_harness}/build/version.txt" _declaration_version)
if(NOT _declaration_version STREQUAL _declared)
  fail("a build without a version from the environment is ${_declared}, not ${_declaration_version}")
endif()

# The rebuild from a version handed in, run from this repository's own module:
# every component keeps its digits, the fourth one becomes the tweak.
function(derive_version _override _build_number _out_version _out_tweak)
  set(BUILD_VERSION "${_override}")
  set(BUILD_NUMBER "${_build_number}")
  set(PROJECT_VERSION "")
  set(PROJECT_VERSION_MAJOR "")
  set(PROJECT_VERSION_MINOR "")
  set(PROJECT_VERSION_PATCH "")
  set(PROJECT_VERSION_TWEAK "")
  include("${PROJECT_DIR}/cmake/project_version.cmake")
  set(${_out_version} "${PROJECT_VERSION}" PARENT_SCOPE)
  set(${_out_tweak} "${PROJECT_VERSION_TWEAK}" PARENT_SCOPE)
endfunction()

derive_version("26.07.0.741" "" _rebuilt _rebuilt_tweak)
if(NOT _rebuilt STREQUAL "26.07.0")
  fail("a version handed to the build as 26.07.0.741 is 26.07.0, not ${_rebuilt}")
endif()
if(NOT _rebuilt_tweak STREQUAL "741")
  fail("the fourth component of 26.07.0.741 is the tweak 741, not ${_rebuilt_tweak}")
endif()

# What this build was made as, and so what its binaries have to report.
if(NOT DEFINED VERSION_OVERRIDE OR VERSION_OVERRIDE STREQUAL "")
  set(_expected "${_declared}")
  set(_expected_source "the version CMakeLists.txt declares")
else()
  derive_version("${VERSION_OVERRIDE}" "${VERSION_BUILD_NUMBER}" _expected _expected_tweak)
  set(_expected_source "the version this build was handed, ${VERSION_OVERRIDE}")
endif()

# The tool runs as a copy of itself inside WORK_DIR, so it reads and writes the
# portable settings beside that copy and nothing another test relies on (#364).
# On Windows the libraries beside the original have to come along, or the copy
# dies before it has printed anything.
set(_tool_dir "${WORK_DIR}/tool")
file(MAKE_DIRECTORY "${_tool_dir}")
file(COPY "${GREP}" DESTINATION "${_tool_dir}")
get_filename_component(_tool_name "${GREP}" NAME)
# What the tool calls itself is Qt's applicationName(), the executable's name
# without its suffix: on Windows it reports `logsquirl_grep`, while the file is
# `logsquirl_grep.exe` (#372).
get_filename_component(_reported_name "${GREP}" NAME_WE)
get_filename_component(_tool_source_dir "${GREP}" DIRECTORY)
file(GLOB _tool_libraries "${_tool_source_dir}/*.dll")
if(_tool_libraries)
  file(COPY ${_tool_libraries} DESTINATION "${_tool_dir}")
endif()

# A ThreadSanitizer build sorts the reports of what this starts the way the
# ctest runner does for every test case (#482); ignored by any other build.
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/TsanReportFilter.cmake")
set(_tsan_dir "${WORK_DIR}/tsan")
logsquirl_tsan_prepare("${_tsan_dir}")

execute_process(
  COMMAND "${_tool_dir}/${_tool_name}" --version
  WORKING_DIRECTORY "${WORK_DIR}"
  OUTPUT_VARIABLE _stdout
  ERROR_VARIABLE _stderr
  RESULT_VARIABLE _result
  TIMEOUT 60
)
logsquirl_tsan_check("${_tsan_dir}" _tsan_failures _tsan_output)
if(NOT _tsan_failures EQUAL 0)
  message(FATAL_ERROR "ThreadSanitizer: ${_tsan_failures} finding(s) in ${_tool_name} --version:\n${_tsan_output}")
endif()
string(REPLACE "\r\n" "\n" _stdout "${_stdout}")
if(NOT _result STREQUAL "0")
  message(FATAL_ERROR "${_tool_name} --version exited with ${_result}\n"
                      "  stdout: ${_stdout}\n  stderr: ${_stderr}"
  )
endif()
if(NOT _stdout MATCHES "${_reported_name} ([^ \n]+)")
  message(FATAL_ERROR "${_reported_name} --version reported no version\n  stdout: ${_stdout}")
endif()
set(_reported "${CMAKE_MATCH_1}")

# The version is reported as declared, character for character. What the build
# appends beyond it -- the tweak of a local build -- is another matter (#372).
string(REPLACE "." "\\." _expected_pattern "${_expected}")
if(NOT _reported MATCHES "^${_expected_pattern}(\\.[0-9]+)?$")
  fail("${_reported_name} reports ${_reported}, not ${_expected}, ${_expected_source}")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")

if(_failures)
  list(JOIN _failures "\n  " _report)
  message(FATAL_ERROR "the version is not the one the project declares:\n  ${_report}")
endif()

message(STATUS "the version is ${_reported}: ${_expected_source}")
