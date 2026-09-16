# Fails when a file of the widget layer reaches for the settings store outside
# the allowlist below: when it names Configuration::get(), the one entry point
# to the settings singleton, without being listed here with a reason (#187).
#
# The engine half of the Settings Policy seam needs no such script: a library
# that consumes a Settings Policy links logsquirl_policies and not
# logsquirl_settings, so reaching for a setting it did not declare is a link
# error (#93, #94). logsquirl_ui cannot be given that gate -- the Options
# Dialog, the Theme wiring, the Shortcuts and the Highlighter Set collection
# all live in it and all legitimately need the settings store. This script is
# the UI half of the seam instead: what the seam forbids is a widget reading a
# setting that no Axis declares, and here that is checked at build time.
#
# Reading is what the seam forbids; writing settings is the Options Dialog's
# job. This script does not tell the two apart, and deliberately does not try:
# both call styles
#
#     Configuration::get().someGetter()
#     const auto& config = Configuration::get();   ...   config.someGetter()
#
# enter through the same expression, and one function reads and writes through
# the same alias, so a regex that claimed to separate them would be wrong more
# often than it was useful. The check therefore keys on the file, and the
# allowlist carries the justification for each one -- writes included.
#
# The allowlist is meant to shrink. An entry that no longer names the settings
# store fails this check too, so a file that has moved behind a Policy leaves
# the list with the commit that moves it.
#
# Usage: cmake -DSOURCES_DIR=<src/ui> -P ui_settings_store_allowlist.cmake

# Script mode sets no policies; if(IN_LIST) needs CMP0057.
cmake_minimum_required(VERSION 3.16)

# Paths relative to SOURCES_DIR. Every entry says why it may hold the store.
set(ALLOWED_FILES
    # Writes settings -- that is what these are for.
    src/optionsdialog.cpp # The Options Dialog: it edits the settings.
    src/plugindialog.cpp  # Persists which plugins are enabled, and auto-load.
    src/quickfindwidget.cpp # Writes back the QuickFind ignore-case toggle.
    src/chartpanel.cpp    # Saves, loads and deletes the Chart Presets.

    # Window chrome, one consumer each: no Axis of its own (#183).
    # minimize-to-tray, confirm-tab-close, toolbar icon size,
    # allow-multiple-windows, show-dashboard -- all read by the main window
    # only, which also reads the view state and logging axes listed below.
    src/mainwindow.cpp

    # Axes no Settings Policy covers yet, deliberately left, tracked in #189:
    # the font (mainFont, useBoldFont, forceFontAntialiasing), the view state
    # (mainLineNumbersVisible, filteredLineNumbersVisible, isOverviewVisible),
    # the search defaults (isSearchIgnoreCaseDefault, isSearchAutoRefreshDefault,
    # isSearchLogicalCombiningDefault), the shortcuts, logging (enableLogging,
    # loggingLevel) and followFileOnLoad.
    src/abstractlogview.cpp # shortcuts.
    src/logtableview.cpp    # font.
    # The Crawler Widget also derives the Decoration Policy from the store
    # rather than being handed it -- tracked in #190.
    src/crawlerwidget.cpp

    # Decided to stay (#186): verifySslPeers, one value with one consumer,
    # outside the lifetime of any Log File.
    src/downloader.cpp
)

if(NOT SOURCES_DIR OR NOT IS_DIRECTORY "${SOURCES_DIR}")
  message(FATAL_ERROR "SOURCES_DIR is not a directory: '${SOURCES_DIR}'")
endif()

file(GLOB_RECURSE SOURCES "${SOURCES_DIR}/*.h" "${SOURCES_DIR}/*.hpp" "${SOURCES_DIR}/*.cpp")
if(NOT SOURCES)
  message(FATAL_ERROR "No sources found under ${SOURCES_DIR}")
endif()

# Tolerates the spacing a formatter may leave: Configuration :: get ( ).
set(STORE_REGEX "Configuration[ \t]*::[ \t]*get[ \t]*\\([ \t]*\\)")

# The offenders are collected as one string, not as a list: a C++ statement
# carries a ';', which list(APPEND) would read as a list separator and split
# the reported line in two.
set(OFFENDERS "")
set(OFFENDERS_COUNT 0)
set(ALLOWED_AND_USED "")
foreach(SOURCE IN LISTS SOURCES)
  file(RELATIVE_PATH RELATIVE_SOURCE "${SOURCES_DIR}" "${SOURCE}")
  file(STRINGS "${SOURCE}" READS REGEX "${STORE_REGEX}")
  if(NOT READS)
    continue()
  endif()
  if(RELATIVE_SOURCE IN_LIST ALLOWED_FILES)
    list(APPEND ALLOWED_AND_USED "${RELATIVE_SOURCE}")
    continue()
  endif()
  foreach(LINE IN LISTS READS)
    string(STRIP "${LINE}" LINE)
    string(APPEND OFFENDERS "\n  ${RELATIVE_SOURCE}: ${LINE}")
    math(EXPR OFFENDERS_COUNT "${OFFENDERS_COUNT} + 1")
  endforeach()
endforeach()

if(OFFENDERS_COUNT GREATER 0)
  message(
    FATAL_ERROR
      "The widget layer reads the settings store outside the allowlist:${OFFENDERS}\n"
      "Take a Settings Policy instead of the store, or add the file to "
      "ALLOWED_FILES in tests/ui_settings_store_allowlist.cmake with a reason (#187).")
endif()

set(STALE "")
foreach(ALLOWED IN LISTS ALLOWED_FILES)
  if(NOT ALLOWED IN_LIST ALLOWED_AND_USED)
    list(APPEND STALE "${ALLOWED}")
  endif()
endforeach()
if(STALE)
  list(JOIN STALE "\n  " REPORT)
  message(
    FATAL_ERROR
      "Allowlisted files that no longer name the settings store:\n  ${REPORT}\n"
      "Drop them from ALLOWED_FILES in tests/ui_settings_store_allowlist.cmake (#187).")
endif()

list(LENGTH SOURCES SOURCES_COUNT)
list(LENGTH ALLOWED_FILES ALLOWED_COUNT)
message(
  STATUS "${SOURCES_COUNT} widget layer files, ${ALLOWED_COUNT} allowlisted, "
         "none of the rest reads the settings store")
