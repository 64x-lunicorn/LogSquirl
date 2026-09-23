# Fails when the third-party code CPM brings in is built with warnings on, or
# when the flag that turns them off has reached LogSquirl's own code (#452).
#
# Two halves of one rule, and the second is the one that would go wrong
# quietly: the flags 3rdparty/CMakeLists.txt sets are the ordinary
# CMAKE_C_FLAGS and CMAKE_CXX_FLAGS, which a directory hands down to every
# directory below it. Set one directory too high and the project would compile
# its own code without a single warning, with nothing failing and nothing to
# see in the log.
#
# Usage: cmake -DQUIET_FLAG=<-w|/W0>
#              -DTHIRD_PARTY=<target=TRUE|FALSE, |-separated>
#              -DPROJECT_TARGETS=<target=TRUE|FALSE, |-separated>
#              -P third_party_builds_quietly.cmake

cmake_minimum_required(VERSION 3.16)

string(REPLACE "|" ";" _third_party "${THIRD_PARTY}")
string(REPLACE "|" ";" _project "${PROJECT_TARGETS}")
if(NOT _third_party)
  message(FATAL_ERROR "THIRD_PARTY is empty")
endif()
if(NOT _project)
  message(FATAL_ERROR "PROJECT_TARGETS is empty")
endif()

# entry_offenders(<entry>... EXPECTED <TRUE|FALSE> SAYING <what a wrong one means> INTO <out_var>)
function(entry_offenders out_var expected saying)
  set(_offenders "")
  foreach(_entry IN LISTS ARGN)
    string(
      REGEX
      REPLACE "=.*$"
              ""
              _name
              "${_entry}"
    )
    string(
      REGEX
      REPLACE "^[^=]*="
              ""
              _quiet
              "${_entry}"
    )
    if(NOT "${_quiet}" STREQUAL "${expected}")
      list(APPEND _offenders "${_name}: ${saying}")
    endif()
  endforeach()
  set(${out_var} "${_offenders}" PARENT_SCOPE)
endfunction()

entry_offenders(_loud TRUE "compiled with warnings on" ${_third_party})
entry_offenders(_silenced FALSE "compiled with the third-party '${QUIET_FLAG}', so nothing checks it" ${_project})

set(_offenders ${_loud} ${_silenced})
if(_offenders)
  list(JOIN _offenders "\n  " _report)
  message(
    FATAL_ERROR
      "'${QUIET_FLAG}' does not separate third-party code from LogSquirl's own:\n  ${_report}\n"
      "It belongs on the targets under 3rdparty/ and on no other -- see cmake/ThirdPartyWarnings.cmake."
  )
endif()

list(LENGTH _third_party _third_party_count)
list(LENGTH _project _project_count)
message(
  STATUS
  "${_third_party_count} third-party targets built quietly, ${_project_count} LogSquirl targets built with the project's warnings"
)
