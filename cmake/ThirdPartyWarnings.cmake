# Third-party code is not LogSquirl's to keep warning-clean, and the noise it
# makes hides the warnings that are (#452): a Windows build printed 1210
# warnings out of CPM packages and 394 command line warnings on top, so a new
# warning in the project's own code was one line among sixteen hundred.
#
# The project's own warnings live in cmake/CompilerWarnings.cmake and reach a
# target through project_warnings, which nothing under 3rdparty/ links. What
# reaches those packages instead is what the top-level CMakeLists.txt puts into
# CMAKE_C_FLAGS and CMAKE_CXX_FLAGS before add_subdirectory(3rdparty) -- /W4 on
# MSVC above all. Taking the noise away therefore needs two steps, in two
# places, because a package can turn warnings back on for itself:
#
#   logsquirl_third_party_quiet_flags()  rewrites those flags in the 3rdparty
#       directory scope, before the first package is added. Inherited by every
#       directory CPM adds below it, and by nothing above it: src/ and tests/
#       keep the flags they had.
#   logsquirl_third_party_build_quietly()  goes over the targets those packages
#       defined, after the fact, and does the same per target. A target's own
#       compile options come after its directory's, so a package that calls
#       add_compile_options(-Wall) on itself -- mimalloc and CRoaring both do --
#       would otherwise win against the directory flags.
#
# The second one also makes their headers system headers for whoever includes
# them, so a warning inside a third-party header is not reported against the
# LogSquirl file that included it.
#
# This mirrors what link time optimization already does one file up: set once,
# after the third-party libraries were added, so it covers them and nothing
# else (CMakeLists.txt, #280).

# logsquirl_third_party_quiet_flags(<out_var> MSVC <ON|OFF> FLAGS "<flags>")
#
# <flags> with the warning level the project sets for its own code taken out
# and the compiler's "no warnings" flag put in.
#
# On MSVC the /W4 is removed rather than overridden: cl reports "D9025:
# overriding '/W4' with '/W0'" once per file when both are on one command line,
# which would trade 1210 warnings for as many command line warnings. Elsewhere
# -w inhibits every warning whatever came before it, so nothing has to be taken
# out.
function(logsquirl_third_party_quiet_flags out_var)
  cmake_parse_arguments(ARG "" "MSVC;FLAGS" "" ${ARGN})

  if(ARG_MSVC)
    string(
      REGEX
      REPLACE "/W[0-4]( |$)"
              "\\1"
              _flags
              "${ARG_FLAGS}"
    )
    set(_flags "${_flags} /W0")
  else()
    set(_flags "${ARG_FLAGS} -w")
  endif()

  string(
    REGEX
    REPLACE "  +"
            " "
            _flags
            "${_flags}"
  )
  string(STRIP "${_flags}" _flags)
  set(${out_var} "${_flags}" PARENT_SCOPE)
endfunction()

# logsquirl_third_party_build_quietly(<dir>)
#
# Every target <dir> and the directories below it define is compiled without
# warnings, and its headers become system headers where LogSquirl includes
# them. Call it after the last package was added, with the 3rdparty directory:
# the directories CPM adds are children of it.
#
# On MSVC the GCC options a package sets on its own targets are taken off as
# well. cl does not know them and reports "D9002: ignoring unknown option" once
# per file for each: the hyperscan fork's unconditional
# `target_compile_options(hs PRIVATE "-fpermissive")` alone is 320 of them in a
# Windows build.
function(logsquirl_third_party_build_quietly dir)
  if(MSVC)
    set(_quiet /W0)
  else()
    set(_quiet -w)
  endif()

  get_property(_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
  foreach(_target IN LISTS _targets)
    get_target_property(_type ${_target} TYPE)
    # A custom target compiles nothing and has no include directories.
    if(_type STREQUAL "UTILITY")
      continue()
    endif()

    get_target_property(_includes ${_target} INTERFACE_INCLUDE_DIRECTORIES)
    if(_includes)
      set_property(TARGET ${_target} PROPERTY INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_includes}")
    endif()

    # An INTERFACE library carries usage requirements and compiles nothing.
    if(_type STREQUAL "INTERFACE_LIBRARY")
      continue()
    endif()

    get_target_property(_options ${_target} COMPILE_OPTIONS)
    if(NOT _options)
      set(_options "")
    endif()
    if(MSVC)
      # cl takes its own options with a dash as well as a slash, and some of
      # them start with -f or -m (-fp:fast, -favor:blend). Those carry their
      # argument after a colon, which no GCC option of this shape does, so the
      # colon is what tells the two apart.
      list(
        FILTER
        _options
        EXCLUDE
        REGEX
        "^-[fm][^:]*$"
      )
    endif()
    list(APPEND _options ${_quiet})
    set_property(TARGET ${_target} PROPERTY COMPILE_OPTIONS "${_options}")
  endforeach()

  get_property(_subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
  foreach(_subdir IN LISTS _subdirs)
    logsquirl_third_party_build_quietly("${_subdir}")
  endforeach()
endfunction()
