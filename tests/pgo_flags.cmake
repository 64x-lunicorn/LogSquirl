# Fails when a build with profile-guided optimization or BOLT gets other
# options than cmake/ProfileGuidedOptimization.cmake documents, or when a
# combination that cannot work configures without saying why (#682).
#
# Two things are held here that nothing else in the build would notice going
# away. A USE build accepts exactly two diagnostics as warnings, GCC's
# -Wmissing-profile (an object the training never ran) and Clang's
# -Wbackend-plugin (a function whose profile record belongs to another copy of
# it), and nothing that would let GCC's mismatching profile or an LTO link
# diagnostic pass (docs/adr/0009, docs/adr/0019). And a USE build
# without its profile stops at configure time with what to run, instead of
# building an unoptimized binary that looks like an optimized one.
#
# Usage: cmake -DMODULE_DIR=<cmake/> -DWORK_DIR=<scratch dir> -P pgo_flags.cmake

cmake_minimum_required(VERSION 3.16)

include(${MODULE_DIR}/ProfileGuidedOptimization.cmake)

# expect_flags(<compile|link> "<expected list>" <argument>...)
function(expect_flags which expected)
  logsquirl_pgo_flags(_compile _link ${ARGN})
  if(which STREQUAL "compile")
    set(_actual "${_compile}")
  else()
    set(_actual "${_link}")
  endif()
  if(NOT "${_actual}" STREQUAL "${expected}")
    message(SEND_ERROR "logsquirl_pgo_flags(${ARGN}) ${which}\n  expected: '${expected}'\n  actual:   '${_actual}'")
  endif()
endfunction()

# expect_problem(<regex or NONE> <argument>...)
function(expect_problem pattern)
  logsquirl_pgo_problem(_problem ${ARGN})
  if(pattern STREQUAL "NONE")
    if(NOT _problem STREQUAL "")
      message(SEND_ERROR "logsquirl_pgo_problem(${ARGN})\n  expected no problem\n  actual: '${_problem}'")
    endif()
  elseif(NOT _problem MATCHES "${pattern}")
    message(SEND_ERROR "logsquirl_pgo_problem(${ARGN})\n  expected a problem matching '${pattern}'\n  actual: '${_problem}'")
  endif()
endfunction()

set(D /work/pgo)
# A USE build's compile options reach the libraries and the trained
# executables only (see LOGSQUIRL_PGO_TRAINED_EXECUTABLES).
set(P
    "$<OR:$<NOT:$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>>,$<IN_LIST:$<TARGET_PROPERTY:NAME>,logsquirl$<SEMICOLON>logsquirl_grep>>"
)

# --- OFF changes nothing, on any compiler ----------------------------------
foreach(_compiler GNU Clang AppleClang)
  expect_flags(compile "" MODE OFF COMPILER_ID ${_compiler} IS_MSVC OFF DIRECTORY ${D} BOLT OFF)
  expect_flags(link "" MODE OFF COMPILER_ID ${_compiler} IS_MSVC OFF DIRECTORY ${D} BOLT OFF)
endforeach()
expect_flags(link "" MODE OFF COMPILER_ID MSVC IS_MSVC ON DIRECTORY ${D} BOLT OFF)

# --- Clang and AppleClang: IR instrumentation, one merged .profdata ---------
foreach(_compiler Clang AppleClang)
  set(_args COMPILER_ID ${_compiler} IS_MSVC OFF DIRECTORY ${D} BOLT OFF)
  expect_flags(compile "-fprofile-generate=${D};-fprofile-update=atomic" MODE GENERATE ${_args})
  expect_flags(link "-fprofile-generate=${D};-fprofile-update=atomic" MODE GENERATE ${_args})
  expect_flags(compile "$<${P}:-fprofile-use=${D}/logsquirl.profdata>;$<${P}:-Wno-error=backend-plugin>" MODE USE
               ${_args}
  )
  # Nothing on the link line: an unused -fprofile-use there is a warning, and
  # with the link's -Werror (ADR 0009) an error.
  expect_flags(link "" MODE USE ${_args})
endforeach()

# --- GCC: the same options on the link, where LTO generates code again -------
set(_gcc COMPILER_ID GNU IS_MSVC OFF DIRECTORY ${D} BOLT OFF)
expect_flags(compile "-fprofile-generate=${D};-fprofile-update=prefer-atomic" MODE GENERATE ${_gcc})
expect_flags(link "-fprofile-generate=${D};-fprofile-update=prefer-atomic" MODE GENERATE ${_gcc})
set(_gcc_use "-fprofile-use=${D};-fprofile-partial-training;-fprofile-correction;-Wno-error=missing-profile")
set(_gcc_use_compile
    "$<${P}:-fprofile-use=${D}>;$<${P}:-fprofile-partial-training>;$<${P}:-fprofile-correction>;$<${P}:-Wno-error=missing-profile>"
)
expect_flags(compile "${_gcc_use_compile}" MODE USE ${_gcc})
# The link generates code for the libraries' objects too, untrained executable
# or not.
expect_flags(link "${_gcc_use}" MODE USE ${_gcc})

# The diagnostics accepted are GCC's missing profile and Clang's backend
# plugin, each on its own compiler; nothing turns warnings off or takes
# -Werror away from GCC's mismatching profile or the link.
foreach(_compiler GNU Clang AppleClang)
  foreach(_mode GENERATE USE)
    logsquirl_pgo_flags(_c _l MODE ${_mode} COMPILER_ID ${_compiler} IS_MSVC OFF DIRECTORY ${D} BOLT ON)
    foreach(_flag IN LISTS _c _l)
      string(REGEX REPLACE "^\\$<.*>:(.*)>$" "\\1" _flag "${_flag}")
      if(_flag MATCHES "^-w$|^-Wno-error$|coverage-mismatch|profile-instr-out-of-date|^-Wno-error=(.*)$")
        if(NOT (_compiler STREQUAL "GNU" AND _flag STREQUAL "-Wno-error=missing-profile")
           AND NOT (_compiler MATCHES "Clang" AND _flag STREQUAL "-Wno-error=backend-plugin")
        )
          message(SEND_ERROR "${_compiler} ${_mode} relaxes a diagnostic it must not: '${_flag}'")
        endif()
      endif()
    endforeach()
  endforeach()
endforeach()

# GCC's missing profile is accepted because the training covers only part of
# LogSquirl, the decision -fprofile-partial-training stands for: the one goes
# with the other (ADR 0019).
logsquirl_pgo_flags(_c _l MODE USE ${_gcc})
foreach(_flags IN ITEMS "${_l}" "${_c}")
  string(FIND "${_flags}" "-fprofile-partial-training" _partial)
  string(FIND "${_flags}" "-Wno-error=missing-profile" _missing)
  if(_partial EQUAL -1 AND NOT _missing EQUAL -1)
    message(SEND_ERROR "GCC accepts a missing profile without training on part of LogSquirl: '${_flags}'")
  endif()
endforeach()

# --- MSVC: link options only, per target, only where LTCG is ----------------
set(_msvc COMPILER_ID MSVC IS_MSVC ON DIRECTORY ${D} BOLT OFF)
set(_ipo "$<BOOL:$<TARGET_PROPERTY:INTERPROCEDURAL_OPTIMIZATION>>")
expect_flags(compile "" MODE GENERATE ${_msvc})
expect_flags(link "$<${_ipo}:/GENPROFILE:PGD=${D}/$<TARGET_PROPERTY:NAME>.pgd>" MODE GENERATE ${_msvc})
expect_flags(compile "" MODE USE ${_msvc})
# Only the trained executables have a .pgd to read: the link of any other
# executable that LTCG covers, a micro-benchmark's, would fail on the missing
# file (LNK1266, #730).
set(_trained "$<IN_LIST:$<TARGET_PROPERTY:NAME>,logsquirl$<SEMICOLON>logsquirl_grep>")
expect_flags(link "$<$<AND:${_ipo},${_trained}>:/USEPROFILE:PGD=${D}/$<TARGET_PROPERTY:NAME>.pgd>" MODE USE ${_msvc})

# --- BOLT: relocations kept; GCC leaves the hot/cold split to BOLT ----------
expect_flags(compile "-fno-reorder-blocks-and-partition" MODE OFF COMPILER_ID GNU IS_MSVC OFF DIRECTORY ${D} BOLT ON)
expect_flags(link "-Wl,--emit-relocs" MODE OFF COMPILER_ID GNU IS_MSVC OFF DIRECTORY ${D} BOLT ON)
expect_flags(compile "" MODE OFF COMPILER_ID Clang IS_MSVC OFF DIRECTORY ${D} BOLT ON)
expect_flags(link "-Wl,--emit-relocs" MODE OFF COMPILER_ID Clang IS_MSVC OFF DIRECTORY ${D} BOLT ON)
expect_flags(compile "${_gcc_use_compile};-fno-reorder-blocks-and-partition" MODE USE COMPILER_ID GNU IS_MSVC OFF DIRECTORY ${D}
             BOLT ON
)

# --- The combinations that cannot work say so ------------------------------
set(_ok COMPILER_VERSION 13.2.0 USE_LTO ON SYSTEM_NAME Linux BOLT OFF DIRECTORY ${D})
expect_problem(NONE MODE OFF COMPILER_ID GNU IS_MSVC OFF ${_ok})
expect_problem(NONE MODE GENERATE COMPILER_ID GNU IS_MSVC OFF ${_ok})
expect_problem(NONE MODE USE COMPILER_ID GNU IS_MSVC OFF ${_ok})
expect_problem("one of OFF, GENERATE and USE" MODE ON COMPILER_ID GNU IS_MSVC OFF ${_ok})
expect_problem("one of OFF, GENERATE and USE" MODE INSTRUMENT COMPILER_ID GNU IS_MSVC OFF ${_ok})
expect_problem("not 'Intel'" MODE GENERATE COMPILER_ID Intel IS_MSVC OFF ${_ok})
expect_problem(NONE MODE OFF COMPILER_ID Intel IS_MSVC OFF ${_ok})
expect_problem("needs GCC 10" MODE USE COMPILER_ID GNU IS_MSVC OFF COMPILER_VERSION 9.4.0 USE_LTO ON SYSTEM_NAME Linux
               BOLT OFF DIRECTORY ${D}
)
expect_problem("needs LOGSQUIRL_USE_LTO=ON" MODE GENERATE COMPILER_ID MSVC IS_MSVC ON COMPILER_VERSION 19.44 USE_LTO OFF
               SYSTEM_NAME Windows BOLT OFF DIRECTORY ${D}
)
expect_problem(NONE MODE GENERATE COMPILER_ID MSVC IS_MSVC ON COMPILER_VERSION 19.44 USE_LTO ON SYSTEM_NAME Windows
               BOLT OFF DIRECTORY ${D}
)
expect_problem("BOLT is for Linux" MODE OFF COMPILER_ID AppleClang IS_MSVC OFF COMPILER_VERSION 21.0 USE_LTO ON
               SYSTEM_NAME Darwin BOLT ON DIRECTORY ${D}
)
expect_problem("BOLT is for Linux" MODE OFF COMPILER_ID MSVC IS_MSVC ON COMPILER_VERSION 19.44 USE_LTO ON
               SYSTEM_NAME Windows BOLT ON DIRECTORY ${D}
)
expect_problem(NONE MODE USE COMPILER_ID GNU IS_MSVC OFF COMPILER_VERSION 13.2.0 USE_LTO ON SYSTEM_NAME Linux BOLT ON
               DIRECTORY ${D}
)

# --- A USE build without its profile stops at configure time ---------------
set(_dir "${WORK_DIR}/profile")
file(REMOVE_RECURSE "${_dir}")
file(MAKE_DIRECTORY "${_dir}")
set(_use MODE USE COMPILER_VERSION 13.2.0 USE_LTO ON SYSTEM_NAME Linux BOLT OFF DIRECTORY ${_dir} CHECK_PROFILE)

expect_problem("no .*logsquirl.profdata.*llvm-profdata merge" COMPILER_ID Clang IS_MSVC OFF ${_use})
expect_problem("no .gcda in .*same build directory" COMPILER_ID GNU IS_MSVC OFF ${_use})
expect_problem("no .pgd in .*pgomgr /merge" COMPILER_ID MSVC IS_MSVC ON ${_use})
# GENERATE writes the profile; it needs none.
expect_problem(NONE MODE GENERATE COMPILER_ID Clang IS_MSVC OFF COMPILER_VERSION 18 USE_LTO ON SYSTEM_NAME Linux BOLT
               OFF DIRECTORY ${_dir} CHECK_PROFILE
)

# Raw profiles alone are not what Clang reads: they have to be merged.
file(WRITE "${_dir}/default_123.profraw" "")
expect_problem("llvm-profdata merge" COMPILER_ID AppleClang IS_MSVC OFF ${_use})
file(WRITE "${_dir}/logsquirl.profdata" "")
expect_problem(NONE COMPILER_ID AppleClang IS_MSVC OFF ${_use})

file(MAKE_DIRECTORY "${_dir}/nested")
file(WRITE "${_dir}/nested/#work#build#src#logdata#x.cpp.gcda" "")
expect_problem(NONE COMPILER_ID GNU IS_MSVC OFF ${_use})

file(WRITE "${_dir}/logsquirl.pgd" "")
expect_problem(NONE COMPILER_ID MSVC IS_MSVC ON ${_use})

file(REMOVE_RECURSE "${_dir}")
