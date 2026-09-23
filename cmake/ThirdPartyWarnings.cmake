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
  # PARSE_ARGV, not ${ARGN}: empty flags would drop out of an expanded list
  # and shift the keywords after them.
  cmake_parse_arguments(PARSE_ARGV 1 ARG "" "MSVC;FLAGS" "")

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
# Windows build. Add the next one to this list, with the package it comes from.
set(LOGSQUIRL_THIRD_PARTY_MSVC_UNKNOWN_OPTIONS
    -fpermissive # variar/hyperscan, on all three of its libraries
)

# logsquirl_strip_msvc_warning_level(<list_var>)
#
# Every warning level out of a list of compile options, leaving the one /W0
# logsquirl_third_party_quiet_flags() put in CMAKE_C_FLAGS and CMAKE_CXX_FLAGS
# as the only one on the command line. Two of them there is what makes cl report
# "D9025: overriding '/W4' with '/W0'" once per file -- the same noise the
# levels were taken out to avoid, which is how a first measurement on CI turned
# 1210 warnings into 327 command line warnings instead of none (#452).
#
# A level does not only arrive as a plain `/W4`. oneTBB sets its own as the
# generator expression `$<$<NOT:$<CXX_COMPILER_ID:Intel>>:/W4>`, and a package
# calling add_definitions("/W3 /D_CRT_SECURE_NO_WARNINGS /nologo") puts three
# options into one list element. So each element is rewritten rather than
# dropped, and a level counts where an option would begin: at the start, after
# a space, or after a generator expression's colon.
function(logsquirl_strip_msvc_warning_level list_var)
  set(_stripped "")
  foreach(_option IN LISTS ${list_var})
    string(
      REGEX
      REPLACE "(^|[ :])[-/]W(all|[0-4])([ >]|$)"
              "\\1\\3"
              _option
              "${_option}"
    )
    string(STRIP "${_option}" _option)
    if(NOT _option STREQUAL "")
      list(APPEND _stripped "${_option}")
    endif()
  endforeach()
  set(${list_var} "${_stripped}" PARENT_SCOPE)
endfunction()

function(logsquirl_third_party_build_quietly dir)
  # On MSVC the level is the /W0 already in CMAKE_C_FLAGS and CMAKE_CXX_FLAGS
  # for this directory and below, and nothing is added per target: a second one
  # on the same command line would be a D9025 rather than a quieter build.
  # Elsewhere -w has to be appended per target, because it only inhibits what
  # comes before it and a package may turn warnings on for its own targets.
  if(MSVC)
    set(_quiet "")
  else()
    set(_quiet -w)
  endif()

  # A level a package set for a whole directory, with add_compile_options() or
  # add_definitions(), reaches its targets without being any target's option.
  if(MSVC)
    get_property(_dir_options DIRECTORY "${dir}" PROPERTY COMPILE_OPTIONS)
    logsquirl_strip_msvc_warning_level(_dir_options)
    set_property(DIRECTORY "${dir}" PROPERTY COMPILE_OPTIONS "${_dir_options}")
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
      # A warning level a package set on its own target has to go, not be
      # overridden: /W0 next to it on one command line is what makes cl report
      # "D9025: overriding '/Wall' with '/W0'" once per file, the same noise
      # under another name that logsquirl_third_party_quiet_flags() takes the
      # trouble to avoid.
      list(
        FILTER
        _options
        EXCLUDE
        REGEX
        "^[-/]W(all|[0-4])$"
      )
      # And the GCC options cl does not know, each of which it reports as
      # "D9002: ignoring unknown option" once per file. Named one by one rather
      # than matched by shape: CMake's MSVC is true for clang-cl too, where
      # -mavx2 and -flto=thin are real options a package may well be selecting
      # its instruction set with, and a function whose job is to turn warnings
      # off has no business changing what code a package generates.
      list(REMOVE_ITEM _options ${LOGSQUIRL_THIRD_PARTY_MSVC_UNKNOWN_OPTIONS})
    endif()
    list(APPEND _options ${_quiet})
    set_property(TARGET ${_target} PROPERTY COMPILE_OPTIONS "${_options}")
  endforeach()

  get_property(_subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
  foreach(_subdir IN LISTS _subdirs)
    logsquirl_third_party_build_quietly("${_subdir}")
  endforeach()
endfunction()
