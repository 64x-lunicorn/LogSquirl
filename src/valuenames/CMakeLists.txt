# Value Names (#647): the Naming Groups with their Naming Rules and Name
# Tables, the rule engine that finds the named values of a Log Line, and the
# CSV a Name Table is imported from and exported to.
#
# It knows no widget and no settings store: the views hand it the Log Lines
# they show, the sidebar and the dialog the groups the user edits.
add_library(
  logsquirl_valuenames STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include/naminggroup.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/nametablecsv.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/valuenamer.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/naminggroup.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/nametablecsv.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/valuenamer.cpp
)

target_include_directories(logsquirl_valuenames PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

# The CSV writer and the group ids come from the utilities.
target_link_libraries(
  logsquirl_valuenames
  PUBLIC project_options
         project_warnings
         logsquirl_utils
         Qt6::Core
)
