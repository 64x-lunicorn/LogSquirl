# Fails when a target that compiles LogSquirl's own code is built without the
# project's warnings (#451). project_warnings carries -Wall -Wextra -Wshadow
# -Wconversion and /W4 together with the /wd4996 the project suppresses, and
# WARNINGS_AS_ERRORS turns them into errors; a target that links neither it nor
# a library that passes it on is compiled unchecked, which is how two test
# plugins went unwarned about for as long as they existed.
#
# Every compiled target under src/ and tests/ is looked at -- the 3rdparty
# libraries CPM brings in are not ours to keep warning-clean, and they live in
# directories of their own.
#
# Usage: cmake -DTARGETS=<target=TRUE|FALSE, |-separated>
#              -P project_warnings_cover_project_targets.cmake

cmake_minimum_required(VERSION 3.16)

string(REPLACE "|" ";" _targets "${TARGETS}")
if(NOT _targets)
  message(FATAL_ERROR "TARGETS is empty")
endif()

set(_offenders "")
foreach(_entry IN LISTS _targets)
  string(REGEX REPLACE "=.*$" "" _name "${_entry}")
  string(REGEX REPLACE "^[^=]*=" "" _reaches_warnings "${_entry}")
  if(NOT _reaches_warnings)
    list(APPEND _offenders "${_name}: links neither project_warnings nor a library that passes it on")
  endif()
endforeach()

if(_offenders)
  list(JOIN _offenders "\n  " _report)
  message(
    FATAL_ERROR
      "These targets compile LogSquirl's own code without the project's warnings:\n  ${_report}\n"
      "Link project_options and project_warnings on them, as every other library and executable does."
  )
endif()

list(LENGTH _targets _count)
message(STATUS "${_count} LogSquirl targets, all of them built with the project's warnings")
