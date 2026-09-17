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
# The allowlist is meant to shrink, down to the writers, the window chrome and
# the Axes decided to stay -- not to zero. An entry that no longer names the
# settings store fails this check too, so a file that has moved behind a Policy
# leaves the list with the commit that moves it.
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
    # minimize-to-tray, confirm-tab-close (read, and written from its
    # "don't ask again"), toolbar icon size, allow-multiple-windows,
    # show-dashboard -- all read by the main window only. It also ticks and
    # writes the View menu's line numbers and overview toggles; what a
    # Presentation shows of them rides the Presentation Policy (#192). The
    # plugin auto-load settings are handed to the Plugin Host by the
    # application, which loads the plugins once for every window (#236, #303).
    # Beside the chrome it reads three Axes decided to stay, below: the shortcuts,
    # logging and followFileOnLoad.
    src/mainwindow.cpp

    # Derives the Policies. The Session is the one entry for a settings change
    # (#245): it re-derives the Policies from the store and hands each changed
    # Axis down. It reads the store for nothing else -- deriveSettingsPolicies()
    # is the only call made on it.
    src/session.cpp

    # Axes decided to stay a direct read -- not waiting for a Policy, and not
    # what "the allowlist is meant to shrink" is about (CONTEXT.md, Settings
    # Policy).
    #
    # The shortcuts: a keyed table of some seventy actions with a codec of its
    # own, not the flat snapshot of values a Policy is. Each widget that owns
    # shortcuts registers them straight from that table -- the main window
    # above, the two files below -- and a Policy would only rewrap the map.
    #
    # Logging (enableLogging, loggingLevel): it configures the process's
    # logger, outside the lifetime of any Log File. The main window re-applies
    # it when the options change; start-up applies it outside the widget layer.
    #
    # followFileOnLoad: one value with one consumer, the main window, read once
    # as a file is opened or the Session restored to decide whether following
    # starts. It is a decision taken at load time, not a setting the Log File
    # keeps.
    src/abstractlogview.cpp # The shortcuts.
    # The shortcuts, and the splitter sizes it reads and writes (window chrome).
    # It also writes the font on zoom, and is the one place that assembles the
    # font (mainFont, useBoldFont, forceFontAntialiasing) its views are handed:
    # decided to stay, no Font Policy, the views are its own children (#194).
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
