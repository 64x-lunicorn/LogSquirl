# Every user-visible string reaches the translation files (#911). lupdate
# extracts tr() only inside a class it knows a context for (Q_OBJECT or
# Q_DECLARE_TR_FUNCTIONS) and Class::tr() calls; a function of its own named
# tr, wrapping QCoreApplication::translate, hides every string passed to it.
# So no source under src/ defines one.
#
# Usage: cmake -DSOURCES_DIR=<src> -P tr_reaches_lupdate.cmake

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
