# Fails when the link step of the project's own targets no longer fails on a
# warning, or when the one warning accepted there outlives the GCC versions it
# was accepted for (#454).
#
# A build with link time optimization generates code twice: once per source
# file, and once more at the link step. CMake puts a target's compile options
# on its compile lines only, so until #454 that second round reported its
# warnings into the log with nothing to make the build fail on them -- GCC 12
# and 13 printed -Wstringop-overflow on every appimage and noble run and
# everything stayed green. What holds that shut now is one flag on the link
# line, and nothing else in the build would notice if it went away again.
#
# The accepted warning is the second reason for a check rather than a comment:
# it is accepted for GCC 12 and 13, and it has to stop being accepted by itself
# once the oldest GCC LogSquirl is built with is newer than that. Here the
# compiler and its version are arguments, so each of those answers can be read
# back without having that compiler.
#
# Usage: cmake -DMODULE_DIR=<cmake/> -P project_warning_flags.cmake

cmake_minimum_required(VERSION 3.16)

include(${MODULE_DIR}/CompilerWarnings.cmake)

# expect_flag(<compile|link> <HAS|LACKS> <flag> OF <argument>...)
function(expect_flag which relation flag of)
  logsquirl_project_warnings(_compile _link ${ARGN})
  if(which STREQUAL "compile")
    set(_flags "${_compile}")
  else()
    set(_flags "${_link}")
  endif()
  if("${flag}" IN_LIST _flags)
    set(_actual HAS)
  else()
    set(_actual LACKS)
  endif()
  if(NOT "${_actual}" STREQUAL "${relation}")
    message(
      SEND_ERROR
        "logsquirl_project_warnings(${ARGN})\n  expected the ${which} flags ${relation} '${flag}'\n  actual:  '${_flags}'"
    )
  endif()
endfunction()

set(GCC12 COMPILER_ID GNU COMPILER_VERSION 12.3.0 MSVC OFF AS_ERRORS ON SANITIZERS OFF)
set(GCC13 COMPILER_ID GNU COMPILER_VERSION 13.2.0 MSVC OFF AS_ERRORS ON SANITIZERS OFF)
set(GCC14 COMPILER_ID GNU COMPILER_VERSION 14.1.0 MSVC OFF AS_ERRORS ON SANITIZERS OFF)
set(GCC16 COMPILER_ID GNU COMPILER_VERSION 16.0.1 MSVC OFF AS_ERRORS ON SANITIZERS OFF)
set(APPLECLANG COMPILER_ID AppleClang COMPILER_VERSION 17.0.0 MSVC OFF AS_ERRORS ON SANITIZERS OFF)
set(MSVC2022 COMPILER_ID MSVC COMPILER_VERSION 19.44.35207.1 MSVC ON AS_ERRORS ON SANITIZERS OFF)

# The link step fails on a warning wherever the compiler generates code there.
expect_flag(link HAS -Werror OF ${GCC12})
expect_flag(link HAS -Werror OF ${GCC16})
expect_flag(link HAS -Werror OF ${APPLECLANG})
# MSVC is left out on purpose: /WX on the linker is about the linker's own LNK
# warnings, not about the code generation this is after
# (docs/adr/0008-lto-link-diagnostics-fail-the-build.md).
expect_flag(link LACKS /WX OF ${MSVC2022})
expect_flag(compile HAS /WX OF ${MSVC2022})

# WARNINGS_AS_ERRORS off is the one way to build without it, and it takes the
# link step with it -- a build that asked for warnings would otherwise still
# get errors out of the link.
set(GCC13_LENIENT COMPILER_ID GNU COMPILER_VERSION 13.2.0 MSVC OFF AS_ERRORS OFF SANITIZERS OFF)
expect_flag(link LACKS -Werror OF ${GCC13_LENIENT})
expect_flag(compile LACKS -Werror OF ${GCC13_LENIENT})

# The accepted warning, and its expiry: GCC 12 and 13 report
# -Wstringop-overflow from the link step for a write that cannot go past
# anything; GCC 14 and newer do not, and by then the flag is gone rather than
# carried along.
expect_flag(link HAS -Wno-error=stringop-overflow OF ${GCC12})
expect_flag(link HAS -Wno-error=stringop-overflow OF ${GCC13})
expect_flag(link LACKS -Wno-error=stringop-overflow OF ${GCC14})
expect_flag(link LACKS -Wno-error=stringop-overflow OF ${GCC16})
# It is accepted at the link step alone. A -Wstringop-overflow from a compile
# is a finding about a write the compiler can see all of, and keeps failing the
# build.
expect_flag(compile LACKS -Wno-error=stringop-overflow OF ${GCC12})
# And it is a GCC answer to a GCC diagnostic.
expect_flag(link LACKS -Wno-error=stringop-overflow OF ${APPLECLANG})

# The warning accepted before this one stays what it was: at compile time, and
# only under a sanitizer.
set(GCC13_SANITIZED COMPILER_ID GNU COMPILER_VERSION 13.2.0 MSVC OFF AS_ERRORS ON SANITIZERS ON)
expect_flag(compile HAS -Wno-error=maybe-uninitialized OF ${GCC13_SANITIZED})
expect_flag(compile LACKS -Wno-error=maybe-uninitialized OF ${GCC13})
