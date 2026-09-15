# Fails when a public header of the log data library includes a TBB header.
# TBB is linked PRIVATE by logsquirl_logdata, so such an include would only
# compile for targets that happen to get TBB some other way (#168).
#
# Usage: cmake -DHEADERS_DIR=<src/logdata/include> -P logdata_public_headers_no_tbb.cmake

if(NOT HEADERS_DIR OR NOT IS_DIRECTORY "${HEADERS_DIR}")
  message(FATAL_ERROR "HEADERS_DIR is not a directory: '${HEADERS_DIR}'")
endif()

file(GLOB_RECURSE _headers "${HEADERS_DIR}/*.h" "${HEADERS_DIR}/*.hpp")
if(NOT _headers)
  message(FATAL_ERROR "No headers found under ${HEADERS_DIR}")
endif()

set(_offenders "")
foreach(_header IN LISTS _headers)
  file(STRINGS "${_header}" _includes REGEX "^[ \t]*#[ \t]*include[ \t]*[<\"](oneapi/)?tbb/")
  foreach(_line IN LISTS _includes)
    list(APPEND _offenders "${_header}: ${_line}")
  endforeach()
endforeach()

if(_offenders)
  list(JOIN _offenders "\n  " _report)
  message(FATAL_ERROR "Public log data headers include TBB:\n  ${_report}")
endif()

list(LENGTH _headers _count)
message(STATUS "${_count} public log data headers, none includes TBB")
