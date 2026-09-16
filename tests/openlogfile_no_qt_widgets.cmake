# Fails when the Open Log File depends on Qt Widgets: when a file under
# src/openlogfile includes a Qt Widgets header, or when logsquirl_openlogfile
# links Qt6::Widgets. It follows a Log File for the desktop application and
# the command line tool alike, so it knows no widget (#244).
#
# Fails too when logsquirl_openlogfile links logsquirl_settings or the UI
# library: it is handed its Policies, and the user interface only shows what
# it tells.
#
# Usage: cmake -DSOURCES_DIR=<src/openlogfile>
#              -DWIDGETS_INCLUDE_DIRS=<Qt6::Widgets include dirs, |-separated>
#              -DLINK_LIBRARIES=<logsquirl_openlogfile link libraries, |-separated>
#              -P openlogfile_no_qt_widgets.cmake

# Script mode sets no policies; if(IN_LIST) needs CMP0057.
cmake_minimum_required(VERSION 3.16)

if(NOT SOURCES_DIR OR NOT IS_DIRECTORY "${SOURCES_DIR}")
  message(FATAL_ERROR "SOURCES_DIR is not a directory: '${SOURCES_DIR}'")
endif()

string(REPLACE "|" ";" _link_libraries "${LINK_LIBRARIES}")
if(NOT _link_libraries)
  message(FATAL_ERROR "LINK_LIBRARIES is empty")
endif()
foreach(_forbidden IN ITEMS Qt6::Widgets Qt::Widgets logsquirl_settings logsquirl_ui)
  if("${_forbidden}" IN_LIST _link_libraries)
    message(FATAL_ERROR "logsquirl_openlogfile links ${_forbidden}: ${_link_libraries}")
  endif()
endforeach()

# Every header Qt Widgets installs, by file name (QWidget, qwidget.h, ...).
# The include directories of Qt6::Widgets also name those of Qt Core and Qt
# Gui; only its own (…/QtWidgets, …/QtWidgets.framework/Headers) count.
string(REPLACE "|" ";" _widgets_dirs "${WIDGETS_INCLUDE_DIRS}")
set(_widgets_headers "")
foreach(_dir IN LISTS _widgets_dirs)
  if(IS_DIRECTORY "${_dir}" AND _dir MATCHES "QtWidgets")
    file(GLOB _found LIST_DIRECTORIES false RELATIVE "${_dir}" "${_dir}/Q*" "${_dir}/q*.h")
    list(APPEND _widgets_headers ${_found})
  endif()
endforeach()
if(NOT _widgets_headers)
  message(FATAL_ERROR "No Qt Widgets headers found in: ${_widgets_dirs}")
endif()

file(GLOB_RECURSE _sources "${SOURCES_DIR}/*.h" "${SOURCES_DIR}/*.hpp" "${SOURCES_DIR}/*.cpp")
if(NOT _sources)
  message(FATAL_ERROR "No sources found under ${SOURCES_DIR}")
endif()

set(_offenders "")
foreach(_source IN LISTS _sources)
  file(STRINGS "${_source}" _includes REGEX "^[ \t]*#[ \t]*include[ \t]*<")
  foreach(_line IN LISTS _includes)
    string(REGEX REPLACE "^[ \t]*#[ \t]*include[ \t]*<([^>]+)>.*$" "\\1" _header "${_line}")
    string(REGEX REPLACE "^QtWidgets/" "" _name "${_header}")
    if(_header MATCHES "^QtWidgets(/|$)" OR _name IN_LIST _widgets_headers)
      list(APPEND _offenders "${_source}: ${_line}")
    endif()
  endforeach()
endforeach()

if(_offenders)
  list(JOIN _offenders "\n  " _report)
  message(FATAL_ERROR "The Open Log File includes Qt Widgets headers:\n  ${_report}")
endif()

list(LENGTH _sources _count)
message(
  STATUS "${_count} Open Log File files, none includes a Qt Widgets header, "
         "and the Open Log File links neither Qt Widgets, the settings store nor the UI")
