# Fails when a source outside the settings library names a shortcut action
# -- ShortcutAction::<Name>, one of the action ids shortcuts.h declares --
# that ShortcutAction::defaultShortcutList() does not list. The list is what
# the shortcut settings show; an action a view or window registers but the
# list leaves out answers to no key the user can give it (#601).
#
# The settings library itself is not scanned: it defines the actions, and it
# names the retired ones that only survive to read old settings.
#
# Usage: cmake -DSOURCES_ROOT=<src>
#              -DEXCLUDED_DIRS=<settings>
#              -DSHORTCUTS_HEADER=<src/settings/include/shortcuts.h>
#              -DSHORTCUTS_SOURCE=<src/settings/src/shortcuts.cpp>
#              -P registered_shortcuts_listed.cmake

# Script mode sets no policies; if(IN_LIST) needs CMP0057.
cmake_minimum_required(VERSION 3.16)

foreach(_file IN ITEMS "${SHORTCUTS_HEADER}" "${SHORTCUTS_SOURCE}")
  if(NOT EXISTS "${_file}")
    message(FATAL_ERROR "Not a file: '${_file}'")
  endif()
endforeach()
if(NOT SOURCES_ROOT OR NOT IS_DIRECTORY "${SOURCES_ROOT}")
  message(FATAL_ERROR "SOURCES_ROOT is not a directory: '${SOURCES_ROOT}'")
endif()
get_filename_component(SOURCES_ROOT "${SOURCES_ROOT}" ABSOLUTE)
string(REPLACE "|" ";" _excluded_dirs "${EXCLUDED_DIRS}")

# The action ids: every "static constexpr auto <Name> =" of shortcuts.h.
file(READ "${SHORTCUTS_HEADER}" _header)
string(REGEX MATCHALL "static constexpr auto [A-Za-z0-9_]+[ \t\r\n]*=" _declarations
                      "${_header}")
set(_actions "")
foreach(_declaration IN LISTS _declarations)
  string(REGEX REPLACE "static constexpr auto ([A-Za-z0-9_]+).*" "\\1" _name "${_declaration}")
  list(APPEND _actions "${_name}")
endforeach()
if(NOT _actions)
  message(FATAL_ERROR "No actions declared in ${SHORTCUTS_HEADER}")
endif()

# The listed actions: each entry of defaultShortcutList() opens with
# "{ <Name>, {", commented-out lines left aside.
file(STRINGS "${SHORTCUTS_SOURCE}" _source_lines)
set(_in_list FALSE)
set(_list_body "")
foreach(_line IN LISTS _source_lines)
  if(_line MATCHES "ShortcutAction::defaultShortcutList\\(\\)")
    set(_in_list TRUE)
  elseif(_in_list AND _line MATCHES "^[ \t]*return ")
    break()
  elseif(_in_list AND NOT _line MATCHES "^[ \t]*//")
    string(APPEND _list_body "${_line}\n")
  endif()
endforeach()
string(REGEX MATCHALL "{[ \t\r\n]*[A-Za-z0-9_]+[ \t\r\n]*,[ \t\r\n]*{" _entries "${_list_body}")
set(_listed "")
foreach(_entry IN LISTS _entries)
  string(REGEX REPLACE "^{[ \t\r\n]*([A-Za-z0-9_]+).*$" "\\1" _name "${_entry}")
  list(APPEND _listed "${_name}")
endforeach()
if(NOT _listed)
  message(FATAL_ERROR "No entries found in defaultShortcutList() of ${SHORTCUTS_SOURCE}")
endif()
foreach(_name IN LISTS _listed)
  if(NOT _name IN_LIST _actions)
    message(FATAL_ERROR "defaultShortcutList() lists '${_name}', which shortcuts.h does not "
                        "declare; the script no longer reads the list right")
  endif()
endforeach()

# Every source outside the excluded directories.
file(GLOB _children LIST_DIRECTORIES TRUE "${SOURCES_ROOT}/*")
set(_sources "")
foreach(_child IN LISTS _children)
  get_filename_component(_name "${_child}" NAME)
  if(IS_DIRECTORY "${_child}" AND NOT _name IN_LIST _excluded_dirs)
    file(GLOB_RECURSE _found "${_child}/*.h" "${_child}/*.hpp" "${_child}/*.cpp")
    list(APPEND _sources ${_found})
  endif()
endforeach()
if(NOT _sources)
  message(FATAL_ERROR "No sources found under ${SOURCES_ROOT}")
endif()

set(_named "")
set(_offenders "")
foreach(_source IN LISTS _sources)
  file(STRINGS "${_source}" _lines REGEX "ShortcutAction::[A-Z]")
  foreach(_line IN LISTS _lines)
    string(REGEX MATCHALL "ShortcutAction::[A-Za-z0-9_]+" _references "${_line}")
    foreach(_reference IN LISTS _references)
      string(REPLACE "ShortcutAction::" "" _name "${_reference}")
      # Types and functions of ShortcutAction are no actions.
      if(NOT _name IN_LIST _actions)
        continue()
      endif()
      list(APPEND _named "${_name}")
      if(NOT _name IN_LIST _listed)
        file(RELATIVE_PATH _relative "${SOURCES_ROOT}" "${_source}")
        list(APPEND _offenders "${_name} (${_relative})")
      endif()
    endforeach()
  endforeach()
endforeach()

if(_offenders)
  list(REMOVE_DUPLICATES _offenders)
  list(JOIN _offenders "\n  " _report)
  message(FATAL_ERROR "Actions named outside the settings but missing from "
                      "defaultShortcutList(), so the shortcut settings do not show them:\n"
                      "  ${_report}")
endif()

list(REMOVE_DUPLICATES _named)
list(LENGTH _named _named_count)
list(LENGTH _listed _listed_count)
if(_named_count EQUAL 0)
  message(FATAL_ERROR "No source under ${SOURCES_ROOT} names an action; the scan is broken")
endif()
message(STATUS "${_named_count} actions named in the sources, all among the "
               "${_listed_count} defaultShortcutList() lists")
