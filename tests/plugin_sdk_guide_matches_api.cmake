# Fails when the plugin developer guide no longer matches the plugin API it
# documents (#598):
#
# - a file of the example plugin differs from the code block the guide shows
#   for it, or has no such block;
# - a declaration of the header -- a #define, an enum constant, a struct
#   member, a function pointer type or an entry point a plugin exports -- is
#   not in the guide as the header spells it (whitespace aside);
# - the guide names a LogSquirl identifier the header does not declare.
#
# The example plugin itself is compiled against the header and loaded by the
# unit tests, so what the guide shows builds and loads.
#
# A code block mirrors an example file when the line before it reads
# <!-- example: <path relative to EXAMPLE_DIR> -->.
#
# Usage: cmake -DGUIDE=<plugin-sdk.md> -DHEADER=<logsquirl_plugin_api.h>
#              -DEXAMPLE_DIR=<docs/plugin-sdk/my_plugin>
#              -P plugin_sdk_guide_matches_api.cmake

cmake_minimum_required(VERSION 3.16)

foreach(_input GUIDE HEADER EXAMPLE_DIR)
  if(NOT EXISTS "${${_input}}")
    message(FATAL_ERROR "${_input} '${${_input}}' does not exist")
  endif()
endforeach()

# A checkout with CRLF line ends compares like one with LF.
function(read_text path out_var)
  file(READ "${path}" _text)
  string(REPLACE "\r" "" _text "${_text}")
  set(${out_var} "${_text}" PARENT_SCOPE)
endfunction()

# Runs of whitespace become one space, so layout does not count.
function(collapse text out_var)
  string(REGEX REPLACE "[ \t\n]+" " " _text "${text}")
  string(STRIP "${_text}" _text)
  set(${out_var} "${_text}" PARENT_SCOPE)
endfunction()

read_text("${GUIDE}" guide)
read_text("${HEADER}" header)
# A ";" would split the lists the declarations are collected in: compare with
# "@" in its place, which neither the header's code nor the guide's uses.
collapse("${guide}" guide_collapsed)
string(REPLACE ";" "@" guide_collapsed "${guide_collapsed}")
string(REPLACE ";" "@" header_code "${header}")
set(failures "")

# ── The example files, shown verbatim ───────────────────────────────────────

set(example_marker "<!-- example: ")
set(shown_examples "")
set(rest "${guide}")
while(TRUE)
  string(FIND "${rest}" "${example_marker}" _at)
  if(_at EQUAL -1)
    break()
  endif()
  string(LENGTH "${example_marker}" _marker_length)
  math(EXPR _at "${_at} + ${_marker_length}")
  string(SUBSTRING "${rest}" ${_at} -1 rest)
  string(FIND "${rest}" " -->\n" _end)
  string(SUBSTRING "${rest}" 0 ${_end} _example)
  string(SUBSTRING "${rest}" ${_end} -1 rest)
  list(APPEND shown_examples "${_example}")

  # The code block right after the marker: its fence line, then the code up
  # to the closing fence.
  if(NOT rest MATCHES "^ -->\n```[a-z]*\n")
    string(APPEND failures "\n  the example marker for '${_example}' is not followed by a code block")
    continue()
  endif()
  string(LENGTH "${CMAKE_MATCH_0}" _fence_length)
  string(SUBSTRING "${rest}" ${_fence_length} -1 rest)
  string(FIND "${rest}" "\n```\n" _close)
  if(_close EQUAL -1)
    string(APPEND failures "\n  the code block for '${_example}' is not closed")
    break()
  endif()
  math(EXPR _code_length "${_close} + 1")
  string(SUBSTRING "${rest}" 0 ${_code_length} _shown)
  string(SUBSTRING "${rest}" ${_code_length} -1 rest)

  if(NOT EXISTS "${EXAMPLE_DIR}/${_example}")
    string(APPEND failures "\n  the guide shows '${_example}', which is not in ${EXAMPLE_DIR}")
    continue()
  endif()
  read_text("${EXAMPLE_DIR}/${_example}" _file)
  if(NOT _shown STREQUAL _file)
    string(APPEND failures "\n  the code block for '${_example}' differs from ${EXAMPLE_DIR}/${_example}")
  endif()
endwhile()

file(GLOB_RECURSE example_files RELATIVE "${EXAMPLE_DIR}" "${EXAMPLE_DIR}/*")
foreach(_file IN LISTS example_files)
  if(NOT _file IN_LIST shown_examples)
    string(APPEND failures "\n  ${EXAMPLE_DIR}/${_file} is not shown in the guide")
  endif()
endforeach()

# ── Every declaration of the header, as the header spells it ────────────────

# The entry points a plugin exports are declared in a comment of the header.
string(REGEX MATCHALL "\n \\*   LOGSQUIRL_PLUGIN_EXPORT [^\n]*@" exports "${header_code}")
if(NOT exports)
  string(APPEND failures "\n  found no exported entry points in ${HEADER}")
endif()
set(expected "")
foreach(_export IN LISTS exports)
  string(REGEX REPLACE "^\n \\*   " "" _export "${_export}")
  list(APPEND expected "${_export}")
endforeach()

# Everything else is code: take the comments out.
set(code "")
set(rest "${header_code}")
while(TRUE)
  string(FIND "${rest}" "/*" _open)
  if(_open EQUAL -1)
    string(APPEND code "${rest}")
    break()
  endif()
  string(SUBSTRING "${rest}" 0 ${_open} _before)
  string(APPEND code "${_before}")
  string(SUBSTRING "${rest}" ${_open} -1 rest)
  string(FIND "${rest}" "*/" _close)
  math(EXPR _close "${_close} + 2")
  string(SUBSTRING "${rest}" ${_close} -1 rest)
endwhile()
string(REGEX REPLACE "//[^\n]*" "" code "${code}")

# The #defines, but for the include guard and the C++ linkage block.
string(REGEX MATCHALL "#define [^\n]*" defines "${code}")
foreach(_define IN LISTS defines)
  if(NOT _define MATCHES "^#define LOGSQUIRL_PLUGIN_API_H")
    list(APPEND expected "${_define}")
  endif()
endforeach()
string(REGEX REPLACE "#[^\n]*" "" code "${code}")
string(REPLACE "extern \"C\" {" "" code "${code}")
collapse("${code}" code)

# The function pointer types: whole statements.
string(REGEX MATCHALL "typedef [^@{}]*\\( \\*LogSquirl[A-Za-z]*Fn \\)[^@]*@" fn_types "${code}")
list(APPEND expected ${fn_types})

# The enums and structs: each constant and member, and the typedef name.
string(REGEX MATCHALL "typedef (enum|struct) {[^}]*} [A-Za-z]+@" types "${code}")
foreach(_type IN LISTS types)
  string(REGEX MATCH "^typedef (enum|struct) {([^}]*)} ([A-Za-z]+@)$" _parts "${_type}")
  set(_kind "${CMAKE_MATCH_1}")
  set(_body "${CMAKE_MATCH_2}")
  list(APPEND expected "} ${CMAKE_MATCH_3}")
  if(_kind STREQUAL "enum")
    string(REPLACE "," ";" _members "${_body}")
  else()
    string(REPLACE "@" ";" _members "${_body}")
  endif()
  foreach(_member IN LISTS _members)
    string(STRIP "${_member}" _member)
    if(NOT _member STREQUAL "")
      list(APPEND expected "${_member}")
    endif()
  endforeach()
endforeach()

foreach(_declaration IN LISTS expected)
  collapse("${_declaration}" _declaration)
  string(FIND "${guide_collapsed}" "${_declaration}" _found)
  if(_found EQUAL -1)
    string(REPLACE "@" ";" _declaration "${_declaration}")
    string(APPEND failures "\n  the guide does not show '${_declaration}' as the header declares it")
  endif()
endforeach()

# ── No identifier the header does not declare ───────────────────────────────

# Names the guide uses that are not the API's: the header's file name, and the
# CMake variable of the example.
set(not_api logsquirl_plugin_api LOGSQUIRL_SDK_DIR)
string(REGEX MATCHALL "(LOGSQUIRL_[A-Z0-9_]+|LogSquirl[A-Za-z0-9_]+|logsquirl_[a-z0-9_]+)"
       named "${guide}")
list(REMOVE_DUPLICATES named)
foreach(_name IN LISTS named)
  if(_name IN_LIST not_api)
    continue()
  endif()
  if(NOT header MATCHES "(^|[^A-Za-z0-9_])${_name}([^A-Za-z0-9_]|$)")
    string(APPEND failures "\n  the guide names '${_name}', which ${HEADER} does not declare")
  endif()
endforeach()

if(failures)
  message(FATAL_ERROR "The plugin developer guide does not match the plugin API:${failures}")
endif()
