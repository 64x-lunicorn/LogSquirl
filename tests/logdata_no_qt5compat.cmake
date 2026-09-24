# Fails when the log data library names a Qt5Compat class in a public header
# or links the Qt5Compat module. Text is decoded through TextEncoding and
# QStringConverter; QTextCodec would bring the deprecated module back onto
# every consumer of the engine (#442).
#
# Usage: cmake -DLOGDATA_DIR=<src/logdata> -P logdata_no_qt5compat.cmake

if(NOT LOGDATA_DIR OR NOT IS_DIRECTORY "${LOGDATA_DIR}/include")
  message(FATAL_ERROR "LOGDATA_DIR is not the log data directory: '${LOGDATA_DIR}'")
endif()

file(GLOB_RECURSE _headers "${LOGDATA_DIR}/include/*.h" "${LOGDATA_DIR}/include/*.hpp")
if(NOT _headers)
  message(FATAL_ERROR "No headers found under ${LOGDATA_DIR}/include")
endif()

set(_offenders "")
foreach(_file IN LISTS _headers "${LOGDATA_DIR}/CMakeLists.txt")
  file(STRINGS "${_file}" _lines REGEX "QTextCodec|QTextDecoder|QTextEncoder|qtextcodec|Core5Compat")
  foreach(_line IN LISTS _lines)
    list(APPEND _offenders "${_file}: ${_line}")
  endforeach()
endforeach()

if(_offenders)
  list(JOIN _offenders "\n  " _report)
  message(FATAL_ERROR "The log data library uses Qt5Compat:\n  ${_report}")
endif()

list(LENGTH _headers _count)
message(STATUS "${_count} public log data headers, none names Qt5Compat")
