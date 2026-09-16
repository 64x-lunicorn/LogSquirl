add_library(
  logsquirl_openlogfile STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include/openlogfile.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/searchautorefresh.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/openlogfile.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/searchautorefresh.cpp
)

set_target_properties(logsquirl_openlogfile PROPERTIES AUTOMOC ON)

target_include_directories(logsquirl_openlogfile PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

# The Open Log File sits above the log data and the log format libraries and
# knows no widget: the desktop application and the command line tool both
# follow a Log File through it (#244). tests/openlogfile_no_qt_widgets.cmake
# guards that it links neither Qt Widgets nor the settings store.
target_link_libraries(
  logsquirl_openlogfile
  PUBLIC project_options
         project_warnings
         logsquirl_logdata
         logsquirl_logformat
         logsquirl_logging
         Qt6::Core
)

if(LOGSQUIRL_USE_LTO)
  set_property(TARGET logsquirl_openlogfile PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()
