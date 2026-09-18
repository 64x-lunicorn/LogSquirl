# Fails when a packaging recipe configures the build with an option this
# project does not have: a `-DLOGSQUIRL_USE_MIMALLOC=OFF` that was renamed
# years ago, or a `-DCMAKE_BUILD_TYPE=RelWithDebugInfo` that CMake silently
# treats as a build type of its own -- an unknown build type has no flags at
# all, so such a package ships an unoptimized binary without debug
# information (#333).
#
# A wrong option does not break the build. CMake accepts any `-D` on the
# command line, stores it in the cache and, for an option no `option()` or
# `set(... CACHE ...)` declares, nothing ever reads it. The packager sees a
# clean build and gets a binary built to different settings than the recipe
# says. Only a check that compares the two sides catches that, which is what
# this script is.
#
# The declared side is scanned out of the project's own CMake files -- every
# `option(NAME ...)` and every `set(NAME ... CACHE ...)` -- plus the variables
# CMake itself documents (`cmake --help-variable-list`), which is where
# CMAKE_BUILD_TYPE and CMAKE_INSTALL_PREFIX come from. The used side is every
# `-D<NAME>` in packaging/. Both sides are read from the tree, so an option
# renamed or dropped in CMakeLists.txt fails here until the recipes follow.
#
# Usage: cmake -DPROJECT_DIR=<repo root> -P packaging_build_options.cmake

# Script mode sets no policies; if(IN_LIST) needs CMP0057.
cmake_minimum_required(VERSION 3.16)

if(NOT PROJECT_DIR)
  message(FATAL_ERROR "PROJECT_DIR is not set")
endif()

# The build types CMake knows. An unknown one is not an error to CMake: it
# just means no optimization and no debug flags (#333).
set(KNOWN_BUILD_TYPES Debug Release RelWithDebInfo MinSizeRel)

# Options a dependency declares, that the project itself therefore does not.
# An entry needs a reason: what declares it, and why a recipe passes it.
set(ALLOWED_EXTERNAL_OPTIONS
    # (none today -- every option the recipes pass is ours or CMake's)
)

# Files under packaging/ whose `-D` belongs to another tool than CMake.
# makensis takes its defines the same way, so the NSIS installer scripts are
# not compared against the CMake options.
set(NON_CMAKE_SUFFIXES .nsi .nsh)

# Files no text is read out of: a recipe cannot hide in an image.
set(BINARY_SUFFIXES .tif .tiff .png .ico .icns .pdf)

# --- what the project declares -------------------------------------------

file(GLOB_RECURSE _cmake_files RELATIVE "${PROJECT_DIR}" "${PROJECT_DIR}/CMakeLists.txt")
file(GLOB_RECURSE _cmake_modules RELATIVE "${PROJECT_DIR}" "${PROJECT_DIR}/*.cmake")
list(APPEND _cmake_files ${_cmake_modules})

set(DECLARED_OPTIONS "")
foreach(_file IN LISTS _cmake_files)
  # A build directory holds generated copies of these files, and a dependency
  # checkout holds its own: both would declare options this project does not.
  # The paths are relative, so this looks at the tree and not at wherever the
  # tree happens to be checked out.
  if(_file MATCHES "(^|/)(build[^/]*|out|_deps|cpm_cache|\\.[^/]+)/")
    continue()
  endif()

  file(READ "${PROJECT_DIR}/${_file}" _content)
  # A declaration spans lines; compare it as one line.
  string(REGEX REPLACE "[\r\n\t]+" " " _content "${_content}")

  string(REGEX MATCHALL "option *\\( *[A-Za-z0-9_]+" _matches "${_content}")
  foreach(_match IN LISTS _matches)
    string(REGEX REPLACE "^option *\\( *" "" _match "${_match}")
    list(APPEND DECLARED_OPTIONS "${_match}")
  endforeach()

  string(REGEX MATCHALL "set *\\( *[A-Za-z0-9_]+[^)]* CACHE " _matches
         "${_content}"
  )
  foreach(_match IN LISTS _matches)
    string(REGEX MATCH "^set *\\( *([A-Za-z0-9_]+)" _ "${_match}")
    list(APPEND DECLARED_OPTIONS "${CMAKE_MATCH_1}")
  endforeach()
endforeach()

if(NOT DECLARED_OPTIONS)
  message(FATAL_ERROR "no option() found under ${PROJECT_DIR}: the scan is broken")
endif()
list(REMOVE_DUPLICATES DECLARED_OPTIONS)

# --- what CMake itself declares ------------------------------------------

execute_process(
  COMMAND ${CMAKE_COMMAND} --help-variable-list
  OUTPUT_VARIABLE _cmake_variables
  RESULT_VARIABLE _result
  ERROR_VARIABLE _error
)
if(NOT _result EQUAL 0)
  message(FATAL_ERROR "cmake --help-variable-list failed: ${_error}")
endif()
string(REGEX REPLACE "[\r\n]+" ";" _cmake_variables "${_cmake_variables}")

# CMake documents a family of variables under a placeholder, CMAKE_<LANG>_FLAGS
# for one. Each becomes a pattern any name of the family matches.
set(CMAKE_VARIABLE_PATTERNS "")
foreach(_variable IN LISTS _cmake_variables)
  string(STRIP "${_variable}" _variable)
  if(NOT _variable MATCHES "^[A-Za-z0-9_<>-]+$")
    continue()
  endif()
  string(REGEX REPLACE "<[A-Za-z0-9_-]+>" "[A-Za-z0-9_]+" _pattern "${_variable}")
  list(APPEND CMAKE_VARIABLE_PATTERNS "^${_pattern}$")
endforeach()

# is_declared(<name> <out var>)
function(is_declared name out_var)
  if("${name}" IN_LIST DECLARED_OPTIONS OR "${name}" IN_LIST ALLOWED_EXTERNAL_OPTIONS)
    set(${out_var} TRUE PARENT_SCOPE)
    return()
  endif()
  foreach(_pattern IN LISTS CMAKE_VARIABLE_PATTERNS)
    if("${name}" MATCHES "${_pattern}")
      set(${out_var} TRUE PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out_var} FALSE PARENT_SCOPE)
endfunction()

# --- what the recipes pass ------------------------------------------------

file(GLOB_RECURSE _recipes RELATIVE "${PROJECT_DIR}/packaging"
     "${PROJECT_DIR}/packaging/*"
)
if(NOT _recipes)
  message(FATAL_ERROR "no file found under ${PROJECT_DIR}/packaging: the scan is broken")
endif()

set(_checked_files 0)
foreach(_recipe IN LISTS _recipes)
  get_filename_component(_suffix "${_recipe}" LAST_EXT)
  string(TOLOWER "${_suffix}" _suffix)
  if(_suffix IN_LIST NON_CMAKE_SUFFIXES OR _suffix IN_LIST BINARY_SUFFIXES)
    continue()
  endif()
  get_filename_component(_name "${_recipe}" NAME)
  if(_name STREQUAL "DS_Store")
    continue()
  endif()

  math(EXPR _checked_files "${_checked_files} + 1")
  file(READ "${PROJECT_DIR}/packaging/${_recipe}" _content)

  string(REGEX MATCHALL "-D[A-Za-z_][A-Za-z0-9_]*" _used "${_content}")
  list(REMOVE_DUPLICATES _used)
  foreach(_option IN LISTS _used)
    string(SUBSTRING "${_option}" 2 -1 _option)
    is_declared("${_option}" _declared)
    if(NOT _declared)
      message(
        SEND_ERROR
          "packaging/${_recipe} passes -D${_option}, an option this project does not declare.\n"
          "  Pass an option that CMakeLists.txt declares, or drop it: CMake accepts\n"
          "  an unknown -D without a word and nothing ever reads it."
      )
    endif()
  endforeach()

  # An unknown build type configures without optimization and without debug
  # information, so it is checked by value and not only by name (#333).
  string(REGEX MATCHALL "-DCMAKE_BUILD_TYPE=[^ \t\r\n\\\\)]+" _build_types
         "${_content}"
  )
  foreach(_build_type IN LISTS _build_types)
    string(REGEX REPLACE "^-DCMAKE_BUILD_TYPE=" "" _build_type "${_build_type}")
    string(REGEX REPLACE "^['\"]|['\"]$" "" _build_type "${_build_type}")
    # A recipe may hand the build type down from a variable of its own.
    if(_build_type MATCHES "[$]" OR _build_type STREQUAL "")
      continue()
    endif()
    if(NOT _build_type IN_LIST KNOWN_BUILD_TYPES)
      string(REPLACE ";" ", " _known "${KNOWN_BUILD_TYPES}")
      message(
        SEND_ERROR
          "packaging/${_recipe} builds CMAKE_BUILD_TYPE=${_build_type}, which is not a build type.\n"
          "  CMake takes an unknown build type as one without flags: no optimization,\n"
          "  no debug information. Use one of: ${_known}."
      )
    endif()
  endforeach()
endforeach()

if(_checked_files EQUAL 0)
  message(FATAL_ERROR "every file under packaging/ was skipped: the scan is broken")
endif()
