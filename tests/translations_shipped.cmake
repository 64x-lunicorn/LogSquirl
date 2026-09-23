# Fails when the translations that exist and the translations that are shipped
# drift apart, or when a shipped one is too unfinished to be usable (#448).
#
# A translation is one src/app/i18n/<code>.ts file. It is shipped when the
# application embeds its compiled <code>.qm -- the build lists those in the
# generated app_translations.qrc -- and offered when the Options Dialog lists
# it, which it does for every entry of src/app/i18n/Languages.xml. The three
# must name the same languages: a .ts that is not built is dead weight, and a
# language that is offered but not built silently stays English.
#
# The rule for offering a language: at least MIN_FINISHED_PERCENT of its
# strings are finished. Below that the user sees a patchwork of their language
# and English, and the language is better not offered at all -- finish it, or
# take it out of Languages.xml and remove its .ts. The English .ts is held to
# the same rule: an unfinished entry there shows the source text, which is
# right today, but it is also what a plural form or a later reworded source
# would silently fall back to.
#
# The share of unfinished strings is reported for every language, so the state
# of the translations is visible in each test run and not only when it fails.
#
# The .ts files must also hold what the source holds today. A string added to
# the source reaches them only when `cmake --build <dir> --target lupdate`
# runs, and until then it is English in every language while the shares above
# still read 100 % -- that is how more than half of the strings once went
# missing. So lupdate is run here too, on copies of the .ts files: a string
# added or reworded since shows up there as unfinished, one removed lowers the
# total, and either way the copy no longer counts what the committed file
# counts. The line numbers lupdate records do not enter the comparison, so
# moving code around does not trip it.
#
# The build writes app_translations.qrc from the same i18n/*.ts the check
# globs, so the two agree by construction; the comparison is there for the
# day a list of languages is written out by hand again.
#
# Usage: cmake -DI18N_DIR=<src/app/i18n> -DAPP_QRC=<app_translations.qrc>
#              -DLUPDATE=<lupdate> -DSOURCES_DIR=<src> -DWORK_DIR=<dir>
#              -P translations_shipped.cmake

# Script mode sets no policies; if(IN_LIST) needs CMP0057.
cmake_minimum_required(VERSION 3.16)

set(MIN_FINISHED_PERCENT 95)

if(NOT I18N_DIR OR NOT IS_DIRECTORY "${I18N_DIR}")
  message(FATAL_ERROR "I18N_DIR is not a directory: '${I18N_DIR}'")
endif()
if(NOT APP_QRC OR NOT EXISTS "${APP_QRC}")
  message(FATAL_ERROR "APP_QRC does not exist: '${APP_QRC}'")
endif()
if(NOT LUPDATE OR NOT EXISTS "${LUPDATE}")
  message(FATAL_ERROR "LUPDATE does not exist: '${LUPDATE}'")
endif()
if(NOT SOURCES_DIR OR NOT IS_DIRECTORY "${SOURCES_DIR}")
  message(FATAL_ERROR "SOURCES_DIR is not a directory: '${SOURCES_DIR}'")
endif()
if(NOT WORK_DIR)
  message(FATAL_ERROR "WORK_DIR is not set")
endif()

# Counts the strings of one .ts file: every message carries one <translation>
# element; a vanished or obsolete one is no longer in the source and does not
# count, an unfinished one counts as not translated whether or not it has text.
function(count_strings ts_file total_var unfinished_var)
  file(READ "${ts_file}" TS)
  # The match must end at the tag's '>': a translated text may hold a ';' (an
  # &amp; is enough), which would split the list of matches.
  string(REGEX MATCHALL "<translation( [^>]*)?>" TRANSLATIONS "${TS}")
  set(TOTAL 0)
  set(UNFINISHED 0)
  foreach(TRANSLATION IN LISTS TRANSLATIONS)
    if(TRANSLATION MATCHES "type=\"(vanished|obsolete)\"")
      continue()
    endif()
    math(EXPR TOTAL "${TOTAL} + 1")
    if(TRANSLATION MATCHES "type=\"unfinished\"")
      math(EXPR UNFINISHED "${UNFINISHED} + 1")
    endif()
  endforeach()
  set(${total_var} ${TOTAL} PARENT_SCOPE)
  set(${unfinished_var} ${UNFINISHED} PARENT_SCOPE)
endfunction()

# The languages that exist.
file(GLOB TS_FILES "${I18N_DIR}/*.ts")
set(EXISTING "")
foreach(TS_FILE IN LISTS TS_FILES)
  get_filename_component(CODE "${TS_FILE}" NAME_WE)
  list(APPEND EXISTING "${CODE}")
endforeach()
if(NOT EXISTING)
  message(FATAL_ERROR "No .ts files found under ${I18N_DIR}")
endif()

# The languages that are shipped.
file(READ "${APP_QRC}" QRC)
string(REGEX MATCHALL "<file>[^<]+\\.qm</file>" QRC_ENTRIES "${QRC}")
set(SHIPPED "")
foreach(ENTRY IN LISTS QRC_ENTRIES)
  string(REGEX REPLACE "^<file>([^<]+)\\.qm</file>$" "\\1" CODE "${ENTRY}")
  list(APPEND SHIPPED "${CODE}")
endforeach()

# The languages that are offered.
file(READ "${I18N_DIR}/Languages.xml" LANGUAGES_XML)
# A commented-out entry is not offered.
string(REGEX REPLACE "<!--([^-]|-[^-])*-->" "" LANGUAGES_XML "${LANGUAGES_XML}")
string(REGEX MATCHALL "ietfCode=\"[^\"]+\"" OFFERED_ENTRIES "${LANGUAGES_XML}")
set(OFFERED "")
foreach(ENTRY IN LISTS OFFERED_ENTRIES)
  string(REGEX REPLACE "^ietfCode=\"([^\"]+)\"$" "\\1" CODE "${ENTRY}")
  list(APPEND OFFERED "${CODE}")
endforeach()

set(PROBLEMS "")
foreach(CODE IN LISTS EXISTING)
  if(NOT CODE IN_LIST SHIPPED)
    string(APPEND PROBLEMS "\n  ${CODE}.ts exists but is not built into the application")
  endif()
  if(NOT CODE IN_LIST OFFERED)
    string(APPEND PROBLEMS "\n  ${CODE}.ts exists but Languages.xml does not offer it")
  endif()
endforeach()
foreach(CODE IN LISTS SHIPPED)
  if(NOT CODE IN_LIST EXISTING)
    string(APPEND PROBLEMS "\n  ${CODE}.qm is built but src/app/i18n has no ${CODE}.ts")
  endif()
endforeach()
foreach(CODE IN LISTS OFFERED)
  if(NOT CODE IN_LIST EXISTING)
    string(APPEND PROBLEMS "\n  Languages.xml offers ${CODE} but src/app/i18n has no ${CODE}.ts")
  endif()
endforeach()

# The share of finished strings.
set(REPORT "")
foreach(CODE IN LISTS EXISTING)
  count_strings("${I18N_DIR}/${CODE}.ts" TOTAL UNFINISHED)
  if(TOTAL EQUAL 0)
    string(APPEND PROBLEMS "\n  ${CODE}.ts has no strings")
    continue()
  endif()
  # Rounded down, so 94.9 % does not pass as 95 %.
  math(EXPR FINISHED_PERCENT "(${TOTAL} - ${UNFINISHED}) * 100 / ${TOTAL}")
  string(APPEND REPORT "\n  ${CODE}: ${UNFINISHED} of ${TOTAL} unfinished, "
                       "${FINISHED_PERCENT} % finished")
  if(FINISHED_PERCENT LESS MIN_FINISHED_PERCENT)
    string(APPEND PROBLEMS "\n  ${CODE}.ts is ${FINISHED_PERCENT} % finished, "
                           "below the ${MIN_FINISHED_PERCENT} % a shipped language needs")
  endif()
endforeach()

# The .ts files against the source of today, through lupdate on copies.
file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")
set(COPIES "")
foreach(CODE IN LISTS EXISTING)
  file(COPY "${I18N_DIR}/${CODE}.ts" DESTINATION "${WORK_DIR}")
  list(APPEND COPIES "${WORK_DIR}/${CODE}.ts")
endforeach()
execute_process(
  COMMAND "${LUPDATE}" -silent -recursive -no-obsolete "${SOURCES_DIR}" -ts ${COPIES}
  WORKING_DIRECTORY "${SOURCES_DIR}"
  RESULT_VARIABLE LUPDATE_RESULT
  OUTPUT_VARIABLE LUPDATE_OUTPUT
  ERROR_VARIABLE LUPDATE_OUTPUT)
if(NOT LUPDATE_RESULT EQUAL 0)
  message(FATAL_ERROR "lupdate failed (${LUPDATE_RESULT}):\n${LUPDATE_OUTPUT}")
endif()
foreach(CODE IN LISTS EXISTING)
  count_strings("${I18N_DIR}/${CODE}.ts" TOTAL UNFINISHED)
  count_strings("${WORK_DIR}/${CODE}.ts" CURRENT_TOTAL CURRENT_UNFINISHED)
  if(NOT TOTAL EQUAL CURRENT_TOTAL OR NOT UNFINISHED EQUAL CURRENT_UNFINISHED)
    string(APPEND PROBLEMS "\n  ${CODE}.ts is behind the source: it has ${TOTAL} strings, "
                           "${UNFINISHED} unfinished; lupdate makes that ${CURRENT_TOTAL}, "
                           "${CURRENT_UNFINISHED} unfinished -- run the lupdate target")
  endif()
endforeach()

message(STATUS "Translations:${REPORT}")

if(PROBLEMS)
  message(
    FATAL_ERROR
      "The translations are not what the application should ship:${PROBLEMS}\n"
      "Run the lupdate target and translate what it adds, or remove a language's .ts "
      "and its Languages.xml entry (#448, CONTRIBUTING.md).")
endif()
