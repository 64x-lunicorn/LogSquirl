# Profile-guided optimization and BOLT (#682).
#
# A build with LOGSQUIRL_PGO=GENERATE is instrumented: every run of one of its
# binaries records which code ran how often into LOGSQUIRL_PGO_DIR. A build
# with LOGSQUIRL_PGO=USE reads that profile and lays out and inlines its code
# for the paths that ran. The training workload is the benchmark mode's
# scenarios, run by the e2e performance suite; CI produces the profile in the
# same run that uses it, so no profile is ever checked in to go stale
# (.github/workflows/pgo.yml, BUILD.md "Profile-guided optimization").
#
# The profile, per compiler, all of it in LOGSQUIRL_PGO_DIR:
# - Clang and AppleClang (IR instrumentation): the instrumented binaries write
#   default_<n>.profraw; `llvm-profdata merge -o logsquirl.profdata *.profraw`
#   (`xcrun llvm-profdata` on macOS) merges them into the one file USE reads.
# - GCC: the instrumented binaries write one .gcda per object, named after the
#   object's absolute path, and merge the counts of every run into it
#   themselves. So the USE build has to be configured in the same build
#   directory as the GENERATE build was.
# - MSVC: the instrumented link writes <target>.pgd, every run of the binary a
#   <target>!<n>.pgc beside it (with VCPROFILE_PATH pointing there);
#   `pgomgr /merge <target>.pgd` merges them, and the USE link reads the .pgd.
#   MSVC's PGO is a property of link time code generation, so it needs
#   LOGSQUIRL_USE_LTO, and covers the targets LTO covers.
#
# Clang and GCC instrument and optimize every C and C++ target of the build,
# the third-party libraries included: Vectorscan's and oneTBB's hot loops are
# as much part of a Search as LogSquirl's own code.
#
# None of this touches the project's warnings: a GENERATE or USE build is held
# to -Werror on its compile and link lines exactly like any other
# (docs/adr/0009-lto-link-diagnostics-fail-the-build.md). A USE build accepts
# two diagnostics as warnings, each with -Wno-error= so it stays in the log:
# - GCC's -Wmissing-profile: "this object never ran during training", true of
#   every object of code the scenarios do not reach. GCC's -Wcoverage-mismatch,
#   an object whose profile no longer fits it, still fails the build.
# - Clang's -Wbackend-plugin, which carries "function control flow change
#   detected (hash mismatch)". Clang finds a function's profile by its name,
#   and an inline function whose copies differ between translation units
#   (macros, include order) matches the record of another copy: measured on
#   the first local USE build, for MessageReceiver::receiveMessage in
#   src/app/main.cpp (#682). Such a function is optimized without a profile,
#   which is what the warning says. Chromium and Firefox build their PGO with
#   the same diagnostic off. A profile from another commit cannot come about
#   here: CI trains the commit it builds, every run.
#
# LOGSQUIRL_BOLT prepares a Linux build for BOLT (llvm-bolt), which rewrites
# the linked executable's layout from a profile of its own: the executables
# keep their relocations (-Wl,--emit-relocs), and GCC does not split
# functions into hot and cold parts itself. The BOLT run happens after the
# build (.github/scripts/pgo.py bolt).
#
# Both are off by default, and a release is built with them only on the
# platforms the A/B numbers in #682 show a clear gain for.

set(LOGSQUIRL_PGO_MODES OFF GENERATE USE)
# The executables the training runs. A USE build compiles the libraries and
# these with the profile, and the own sources of every other executable (the
# tests, the micro-benchmarks, logsquirl_portable) without: Clang finds a
# function's profile by its name, so a benchmark's main() would meet
# logsquirl's, fail its hash check and, with -Werror, the build.
set(LOGSQUIRL_PGO_TRAINED_EXECUTABLES logsquirl logsquirl_grep)
# The merged profile of Clang and AppleClang, in LOGSQUIRL_PGO_DIR.
set(LOGSQUIRL_PGO_CLANG_PROFILE logsquirl.profdata)

#   logsquirl_pgo_flags(<compile_out> <link_out>
#                       MODE <OFF|GENERATE|USE>
#                       COMPILER_ID <CMAKE_CXX_COMPILER_ID>
#                       IS_MSVC <ON|OFF>
#                       DIRECTORY <profile directory>
#                       BOLT <ON|OFF>)
#
# The compile and link options a build of that compiler gets. Generator
# expressions in them are meant for add_compile_options()/add_link_options():
# MSVC's link options name each target's own .pgd, and are given only to the
# targets link time code generation covers. Takes no decision about whether
# the combination is valid; logsquirl_pgo_problem() does.
function(logsquirl_pgo_flags compile_out link_out)
  cmake_parse_arguments(ARG "" "MODE;COMPILER_ID;IS_MSVC;DIRECTORY;BOLT" "" ${ARGN})

  set(compile "")
  set(link "")
  set(dir "${ARG_DIRECTORY}")
  # For a USE build's compile options: a library, or a trained executable.
  list(JOIN LOGSQUIRL_PGO_TRAINED_EXECUTABLES "$<SEMICOLON>" trained)
  set(profiled
      "$<OR:$<NOT:$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>>,$<IN_LIST:$<TARGET_PROPERTY:NAME>,${trained}>>"
  )

  if(ARG_IS_MSVC)
    set(ipo "$<BOOL:$<TARGET_PROPERTY:INTERPROCEDURAL_OPTIMIZATION>>")
    set(pgd "${dir}/$<TARGET_PROPERTY:NAME>.pgd")
    if(ARG_MODE STREQUAL "GENERATE")
      list(APPEND link "$<${ipo}:/GENPROFILE:PGD=${pgd}>")
    elseif(ARG_MODE STREQUAL "USE")
      list(APPEND link "$<${ipo}:/USEPROFILE:PGD=${pgd}>")
    endif()
  elseif(ARG_COMPILER_ID MATCHES "Clang")
    if(ARG_MODE STREQUAL "GENERATE")
      # Atomic counters: LogSquirl indexes and searches on several threads,
      # and counts lost to races would train the layout on a wrong picture.
      list(APPEND compile "-fprofile-generate=${dir}" -fprofile-update=atomic)
      # The driver links the profile runtime only when told at the link.
      list(APPEND link "-fprofile-generate=${dir}" -fprofile-update=atomic)
    elseif(ARG_MODE STREQUAL "USE")
      # The profile goes into the intermediate code, which the link time code
      # generation reads; the link line needs nothing, and an unused
      # -fprofile-use there would be a warning, and with -Werror an error.
      list(APPEND compile "$<${profiled}:-fprofile-use=${dir}/${LOGSQUIRL_PGO_CLANG_PROFILE}>"
           "$<${profiled}:-Wno-error=backend-plugin>"
      )
    endif()
  elseif(ARG_COMPILER_ID STREQUAL "GNU")
    if(ARG_MODE STREQUAL "GENERATE")
      set(flags "-fprofile-generate=${dir}" -fprofile-update=prefer-atomic)
      list(APPEND compile ${flags})
      list(APPEND link ${flags})
    elseif(ARG_MODE STREQUAL "USE")
      # -fprofile-partial-training: code the training never ran is optimized
      # as without a profile, not for size as if it were cold. The training
      # covers the benchmark mode's scenarios, not everything a user does.
      # -fprofile-correction: counters of several threads can disagree
      # slightly even when updated atomically where possible.
      # The link generates code again (LTO), so it needs the same options.
      set(flags
          "-fprofile-use=${dir}"
          -fprofile-partial-training
          -fprofile-correction
          -Wno-error=missing-profile
      )
      foreach(flag IN LISTS flags)
        list(APPEND compile "$<${profiled}:${flag}>")
      endforeach()
      list(APPEND link ${flags})
    endif()
  endif()

  if(ARG_BOLT AND NOT ARG_IS_MSVC)
    if(ARG_COMPILER_ID STREQUAL "GNU")
      # BOLT splits hot and cold code itself, from its own profile.
      list(APPEND compile -fno-reorder-blocks-and-partition)
    endif()
    list(APPEND link -Wl,--emit-relocs)
  endif()

  set(${compile_out} "${compile}" PARENT_SCOPE)
  set(${link_out} "${link}" PARENT_SCOPE)
endfunction()

#   logsquirl_pgo_problem(<out>
#                         MODE <mode> COMPILER_ID <id> COMPILER_VERSION <v>
#                         IS_MSVC <ON|OFF> USE_LTO <ON|OFF>
#                         SYSTEM_NAME <CMAKE_SYSTEM_NAME> BOLT <ON|OFF>
#                         DIRECTORY <profile directory>
#                         [CHECK_PROFILE])
#
# Sets <out> to what is wrong with the combination, or to an empty string.
# CHECK_PROFILE also looks into DIRECTORY for the profile a USE build reads.
function(logsquirl_pgo_problem out)
  cmake_parse_arguments(
    ARG "CHECK_PROFILE" "MODE;COMPILER_ID;COMPILER_VERSION;IS_MSVC;USE_LTO;SYSTEM_NAME;BOLT;DIRECTORY" "" ${ARGN}
  )
  set(problem "")

  if(NOT ARG_MODE IN_LIST LOGSQUIRL_PGO_MODES)
    set(problem "LOGSQUIRL_PGO is '${ARG_MODE}'; it is one of OFF, GENERATE and USE.")
  elseif(NOT ARG_MODE STREQUAL "OFF" AND NOT ARG_IS_MSVC AND NOT ARG_COMPILER_ID MATCHES "Clang"
         AND NOT ARG_COMPILER_ID STREQUAL "GNU"
  )
    set(problem "LOGSQUIRL_PGO=${ARG_MODE} supports Clang, AppleClang, GCC and MSVC, not '${ARG_COMPILER_ID}'.")
  elseif(NOT ARG_MODE STREQUAL "OFF" AND ARG_IS_MSVC AND NOT ARG_USE_LTO)
    set(problem
        "LOGSQUIRL_PGO=${ARG_MODE} with MSVC needs LOGSQUIRL_USE_LTO=ON: MSVC's profile-guided optimization is part of its link time code generation."
    )
  elseif(ARG_MODE STREQUAL "USE" AND ARG_COMPILER_ID STREQUAL "GNU" AND ARG_COMPILER_VERSION VERSION_LESS 10)
    set(problem "LOGSQUIRL_PGO=USE needs GCC 10 or newer (-fprofile-partial-training), not ${ARG_COMPILER_VERSION}.")
  elseif(ARG_BOLT AND (ARG_IS_MSVC OR NOT ARG_SYSTEM_NAME STREQUAL "Linux"))
    set(problem "LOGSQUIRL_BOLT is for Linux builds with GCC or Clang: BOLT rewrites ELF executables only.")
  elseif(ARG_MODE STREQUAL "USE" AND ARG_CHECK_PROFILE)
    set(dir "${ARG_DIRECTORY}")
    if(ARG_IS_MSVC)
      file(GLOB found "${dir}/*.pgd")
      set(missing "no .pgd in ${dir}")
      set(howto
          "Build with LOGSQUIRL_PGO=GENERATE and LOGSQUIRL_PGO_DIR=${dir}, run the training with VCPROFILE_PATH=${dir}, then `pgomgr /merge <target>.pgd` for each .pgd there."
      )
    elseif(ARG_COMPILER_ID MATCHES "Clang")
      set(found "")
      if(EXISTS "${dir}/${LOGSQUIRL_PGO_CLANG_PROFILE}")
        set(found "${dir}/${LOGSQUIRL_PGO_CLANG_PROFILE}")
      endif()
      set(missing "no ${dir}/${LOGSQUIRL_PGO_CLANG_PROFILE}")
      set(howto
          "Build with LOGSQUIRL_PGO=GENERATE and LOGSQUIRL_PGO_DIR=${dir}, run the training, then `llvm-profdata merge -o ${dir}/${LOGSQUIRL_PGO_CLANG_PROFILE} ${dir}/*.profraw` (`xcrun llvm-profdata` on macOS)."
      )
    else()
      file(GLOB_RECURSE found "${dir}/*.gcda")
      set(missing "no .gcda in ${dir}")
      set(howto
          "Build with LOGSQUIRL_PGO=GENERATE and LOGSQUIRL_PGO_DIR=${dir} in this same build directory (GCC names a profile after its object's absolute path), and run the training."
      )
    endif()
    if(NOT found)
      set(problem "LOGSQUIRL_PGO=USE found no profile: ${missing}. ${howto}")
    endif()
  endif()

  set(${out} "${problem}" PARENT_SCOPE)
endfunction()
