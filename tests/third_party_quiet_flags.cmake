# Fails when the flags third-party code is compiled with still carry the
# warning level LogSquirl holds its own code to, or when MSVC would be handed
# two warning levels at once (#452).
#
# The second one is the whole reason /W4 is removed rather than overridden: cl
# answers a command line with both by reporting "D9025: overriding '/W4' with
# '/W0'" once per file, which is the same noise under a different name.
#
# Usage: cmake -DMODULE_DIR=<cmake/> -P third_party_quiet_flags.cmake

cmake_minimum_required(VERSION 3.16)

include(${MODULE_DIR}/ThirdPartyWarnings.cmake)

# expect(<expected flags> <argument>...)
function(expect expected)
  logsquirl_third_party_quiet_flags(_actual ${ARGN})
  if(NOT "${_actual}" STREQUAL "${expected}")
    message(SEND_ERROR "logsquirl_third_party_quiet_flags(${ARGN})\n  expected: '${expected}'\n  actual:   '${_actual}'")
  endif()
endfunction()

# MSVC: the project's warning level goes, one that says nothing arrives. The
# flags the top-level CMakeLists.txt sets around it are left alone -- /external
# is what makes a system include directory quiet there, and /bigobj is not
# about warnings at all.
expect("/W0" MSVC ON FLAGS "/W4")
expect("/DWIN32 /bigobj /W0" MSVC ON FLAGS "/DWIN32 /W4 /bigobj")
expect("/experimental:external /external:W0 /W0" MSVC ON FLAGS "/experimental:external /external:W0 /W4")
# Whatever level was there, there is one left, and it is the quiet one.
expect("/W0" MSVC ON FLAGS "/W0")
expect("/W0" MSVC ON FLAGS "/W3")
expect("/DNDEBUG /W0" MSVC ON FLAGS "/DNDEBUG")

# GCC and Clang: -w inhibits every warning whatever came before it, so nothing
# has to be taken out and -Wall may stay where it is.
expect("-Wall -Wextra -w" MSVC OFF FLAGS "-Wall -Wextra")
expect("-w" MSVC OFF FLAGS "")
expect("-march=armv8.1-a -w" MSVC OFF FLAGS "-march=armv8.1-a")
# A GCC build must keep its own -W flags: they are the project's, not MSVC's,
# and the MSVC branch is the only one that removes anything.
expect("-Wa,--noexecstack -w" MSVC OFF FLAGS "-Wa,--noexecstack")
