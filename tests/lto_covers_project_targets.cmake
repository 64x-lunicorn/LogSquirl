# Fails when link time optimization does not cover every target of LogSquirl
# alike: when LOGSQUIRL_USE_LTO is on and a library, application, test or
# benchmark is built without it, or when it is off and a target still asks for
# it (#280). LTO is set in one place, the top-level CMakeLists.txt; a target
# that sets INTERPROCEDURAL_OPTIMIZATION itself, or one added before that
# place, shows up here.
#
# Usage: cmake -DUSE_LTO=<ON|OFF>
#              -DTARGETS=<target=its INTERPROCEDURAL_OPTIMIZATION, |-separated>
#              -P lto_covers_project_targets.cmake

cmake_minimum_required(VERSION 3.16)

string(REPLACE "|" ";" _targets "${TARGETS}")
if(NOT _targets)
  message(FATAL_ERROR "TARGETS is empty")
endif()

set(_offenders "")
foreach(_entry IN LISTS _targets)
  string(REGEX REPLACE "=.*$" "" _name "${_entry}")
  string(REGEX REPLACE "^[^=]*=" "" _ipo "${_entry}")
  if(USE_LTO AND NOT _ipo)
    list(APPEND _offenders "${_name}: built without link time optimization")
  elseif(NOT USE_LTO AND _ipo)
    list(APPEND _offenders "${_name}: INTERPROCEDURAL_OPTIMIZATION is '${_ipo}' with LOGSQUIRL_USE_LTO off")
  endif()
endforeach()

if(_offenders)
  list(JOIN _offenders "\n  " _report)
  message(FATAL_ERROR "Link time optimization does not follow LOGSQUIRL_USE_LTO (${USE_LTO}):\n  ${_report}")
endif()

list(LENGTH _targets _count)
message(STATUS "${_count} LogSquirl targets, link time optimization ${USE_LTO} for all of them")
