# The Log Format Catalog reads the Log Formats of enabled plugins without
# knowing plugins (#793): whoever owns both the plugin layer and the Catalog
# hands it their files. So no file of the logformat library includes a header
# of the plugin layer, and the library links no plugin library.
#
# Usage: cmake -DSOURCES_DIR=<src/logformat> -DPLUGINS_INCLUDE_DIR=<src/plugins/include>
#              "-DLINK_LIBRARIES=<lib|lib|...>" -P logformat_no_plugins.cmake

cmake_minimum_required(VERSION 3.16)

if(NOT SOURCES_DIR OR NOT IS_DIRECTORY "${SOURCES_DIR}")
  message(FATAL_ERROR "SOURCES_DIR is not a directory: '${SOURCES_DIR}'")
endif()
if(NOT PLUGINS_INCLUDE_DIR OR NOT IS_DIRECTORY "${PLUGINS_INCLUDE_DIR}")
  message(FATAL_ERROR "PLUGINS_INCLUDE_DIR is not a directory: '${PLUGINS_INCLUDE_DIR}'")
endif()

string(REPLACE "|" ";" _link_libraries "${LINK_LIBRARIES}")
if(NOT _link_libraries)
  message(FATAL_ERROR "LINK_LIBRARIES is empty")
endif()
foreach(_library IN LISTS _link_libraries)
  if(_library MATCHES "^logsquirl_plugin")
    message(FATAL_ERROR "logsquirl_logformat links the plugin layer: ${_link_libraries}")
  endif()
endforeach()

file(GLOB _plugin_headers LIST_DIRECTORIES false RELATIVE "${PLUGINS_INCLUDE_DIR}"
     "${PLUGINS_INCLUDE_DIR}/*.h")
if(NOT _plugin_headers)
  message(FATAL_ERROR "No plugin layer headers found in ${PLUGINS_INCLUDE_DIR}")
endif()

file(GLOB_RECURSE _sources "${SOURCES_DIR}/*.h" "${SOURCES_DIR}/*.hpp" "${SOURCES_DIR}/*.cpp")
if(NOT _sources)
  message(FATAL_ERROR "No sources found under ${SOURCES_DIR}")
endif()

set(_offenders "")
foreach(_source IN LISTS _sources)
  file(STRINGS "${_source}" _includes REGEX "^[ \t]*#[ \t]*include[ \t]*[<\"]")
  foreach(_line IN LISTS _includes)
    string(REGEX REPLACE "^[ \t]*#[ \t]*include[ \t]*[<\"]([^>\"]+)[>\"].*$" "\\1" _header "${_line}")
    get_filename_component(_name "${_header}" NAME)
    if(_name IN_LIST _plugin_headers)
      list(APPEND _offenders "${_source}: ${_line}")
    endif()
  endforeach()
endforeach()

if(_offenders)
  list(JOIN _offenders "\n  " _report)
  message(FATAL_ERROR "The logformat library includes plugin layer headers:\n  ${_report}")
endif()

list(LENGTH _sources _count)
message(STATUS "${_count} logformat files, none includes a plugin layer header, "
               "and the library does not link the plugin layer")
