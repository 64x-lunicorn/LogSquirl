# Every user-visible string reaches the translation files (#911). lupdate
# extracts tr() only inside a class it knows a context for (Q_OBJECT or
# Q_DECLARE_TR_FUNCTIONS) and Class::tr() calls; a function of its own named
# tr, wrapping QCoreApplication::translate, hides every string passed to it.
# So no source under src/ defines one.
#
# Usage: cmake -DSOURCES_DIR=<src> -DTS_FILE=<src/app/i18n/en.ts> -P tr_reaches_lupdate.cmake

cmake_minimum_required(VERSION 3.16)

if(NOT SOURCES_DIR OR NOT IS_DIRECTORY "${SOURCES_DIR}")
  message(FATAL_ERROR "SOURCES_DIR is not a directory: '${SOURCES_DIR}'")
endif()

file(GLOB_RECURSE _sources "${SOURCES_DIR}/*.h" "${SOURCES_DIR}/*.hpp" "${SOURCES_DIR}/*.cpp")
if(NOT _sources)
  message(FATAL_ERROR "No sources found under ${SOURCES_DIR}")
endif()

set(_offenders "")
foreach(_source IN LISTS _sources)
  file(STRINGS "${_source}" _definitions REGEX "^[ \t]*(static[ \t]+)?QString[ \t]+tr[ \t]*\\(")
  if(_definitions)
    file(RELATIVE_PATH _relative "${SOURCES_DIR}" "${_source}")
    list(APPEND _offenders "${_relative}")
  endif()
endforeach()

if(_offenders)
  list(JOIN _offenders "\n  " _list)
  message(FATAL_ERROR
          "These files define a tr() of their own, whose strings lupdate does not extract; "
          "declare Q_DECLARE_TR_FUNCTIONS in a class and call Class::tr() instead:\n  ${_list}")
endif()

# And the strings themselves arrive: every plain tr( "..." ) literal of a .cpp
# is a source text of the translation file TS_FILE. Literals with escapes or
# split over lines are left out.
if(NOT TS_FILE OR NOT EXISTS "${TS_FILE}")
  message(FATAL_ERROR "TS_FILE does not exist: '${TS_FILE}'")
endif()
file(READ "${TS_FILE}" _ts)

set(_missing "")
foreach(_source IN LISTS _sources)
  if(NOT _source MATCHES "\\.cpp$")
    continue()
  endif()
  file(READ "${_source}" _code)
  # A ; would split the list of calls; code commented out does not count.
  string(REPLACE ";" "<semicolon>" _code "${_code}")
  string(REGEX REPLACE "//[^\n]*" "" _code "${_code}")
  string(REGEX MATCHALL "tr\\( \"[^\"\\\\\n]+\" \\)" _calls "${_code}")
  foreach(_call IN LISTS _calls)
    string(REGEX REPLACE "^tr\\( \"(.*)\" \\)$" "\\1" _text "${_call}")
    string(REPLACE "<semicolon>" ";" _text "${_text}")
    string(REPLACE "&" "&amp;" _text "${_text}")
    string(REPLACE "<" "&lt;" _text "${_text}")
    string(REPLACE ">" "&gt;" _text "${_text}")
    string(REPLACE "'" "&apos;" _text "${_text}")
    string(FIND "${_ts}" "<source>${_text}</source>" _at)
    if(_at EQUAL -1)
      file(RELATIVE_PATH _relative "${SOURCES_DIR}" "${_source}")
      list(APPEND _missing "${_relative}: ${_text}")
    endif()
  endforeach()
endforeach()

if(_missing)
  list(JOIN _missing "\n  " _list)
  message(FATAL_ERROR
          "These strings are not in ${TS_FILE}; run the lupdate target, and if they still "
          "are not, lupdate cannot see the tr() they are passed to:\n  ${_list}")
endif()
