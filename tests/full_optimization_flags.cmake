# Fails when the RelWithDebInfo flags the releases are built with are not fully
# optimized, or lose the debug information crash reports are symbolicated
# with: when MSVC still inlines only functions marked inline (/Ob1) or links
# incrementally, when GCC or Clang stay at -O2, or when -g, /debug or the
# release defines are dropped on the way (#280).
#
# Usage: cmake -DMODULE_DIR=<cmake/> -P full_optimization_flags.cmake

cmake_minimum_required(VERSION 3.16)

include(${MODULE_DIR}/FullOptimization.cmake)

# expect(<expected flags> <argument>...)
function(expect expected)
  logsquirl_full_optimization_flags(_actual ${ARGN})
  if(NOT "${_actual}" STREQUAL "${expected}")
    message(SEND_ERROR "logsquirl_full_optimization_flags(${ARGN})\n  expected: '${expected}'\n  actual:   '${_actual}'")
  endif()
endfunction()

# GCC and Clang: CMake's RelWithDebInfo defaults, -O2 becomes -O3.
expect("-O3 -g -DNDEBUG" KIND COMPILE MSVC OFF FLAGS "-O2 -g -DNDEBUG")
expect(" -O3 -g -DNDEBUG" KIND COMPILE MSVC OFF FLAGS " -O2 -g -DNDEBUG")
expect("-g -O3 -DNDEBUG" KIND COMPILE MSVC OFF FLAGS "-g -O1 -DNDEBUG")
# Already fully optimized, or optimized for size on purpose: left alone.
expect("-O3 -g -DNDEBUG" KIND COMPILE MSVC OFF FLAGS "-O3 -g -DNDEBUG")
expect("-Os -g -DNDEBUG" KIND COMPILE MSVC OFF FLAGS "-Os -g -DNDEBUG")
# The linker of GCC and Clang has nothing to change.
expect("" KIND LINK MSVC OFF FLAGS "")
expect("-Wl,--gc-sections" KIND LINK MSVC OFF FLAGS "-Wl,--gc-sections")

# MSVC: inline any suitable function, not only those marked inline.
expect("/O2 /Ob2 /DNDEBUG" KIND COMPILE MSVC ON FLAGS "/O2 /Ob1 /DNDEBUG")
expect("/Zi /O2 /Ob2 /DNDEBUG" KIND COMPILE MSVC ON FLAGS "/Zi /O2 /Ob1 /DNDEBUG")
expect("-O2 /Ob2 -DNDEBUG" KIND COMPILE MSVC ON FLAGS "-O2 -Ob1 -DNDEBUG")
expect("/O2 /DNDEBUG /Ob2" KIND COMPILE MSVC ON FLAGS "/O2 /DNDEBUG")

# MSVC link: keep the PDB (/debug), but link like a release build. An
# incremental link turns off the removal of unreferenced functions and the
# folding of identical ones that /debug otherwise also turns off.
expect("/debug /INCREMENTAL:NO /OPT:REF /OPT:ICF" KIND LINK MSVC ON FLAGS "/debug /INCREMENTAL")
expect("/debug /INCREMENTAL:NO /OPT:REF /OPT:ICF" KIND LINK MSVC ON FLAGS "/debug /INCREMENTAL:YES")
expect("/DEBUG /INCREMENTAL:NO /OPT:REF /OPT:ICF" KIND LINK MSVC ON FLAGS "/DEBUG")
expect("/debug /INCREMENTAL:NO /OPT:REF /OPT:ICF" KIND LINK MSVC ON FLAGS "/debug /INCREMENTAL:NO /OPT:REF /OPT:ICF")
