# The order of the steps that open a Log File in a window (#949): one pure
# decision the window asks and carries out. It knows no window, no plugin
# and no file; it takes what decompressing would do from the compression
# library.
add_library(
  logsquirl_opensteps STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include/logfilelifetime.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/openstep.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/openstep.cpp
)

target_include_directories(logsquirl_opensteps PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(
  logsquirl_opensteps
  PUBLIC project_options
         project_warnings
         logsquirl_compression
)
