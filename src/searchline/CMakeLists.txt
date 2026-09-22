# The Search Line: the pattern of a Search, the buttons that say how it is
# read, and what the line says about the Search that runs (#399).
#
# It knows no widget: the Crawler Widget hands it every event and mirrors
# what it shows. tests/searchline_no_qt_widgets.cmake guards that no file of
# it includes a Qt Widgets header and that it links neither Qt Widgets, the
# settings store nor the UI library.
add_library(
  logsquirl_searchline STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include/predefinedfilter.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/searchline.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/searchline.cpp
)

target_include_directories(logsquirl_searchline PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

# The Search Session's state and the line types live in the log data library,
# the auto-refresh state in the Open Log File, the pattern in the regex one.
target_link_libraries(
  logsquirl_searchline
  PUBLIC project_options
         project_warnings
         logsquirl_policies
         logsquirl_logdata
         logsquirl_openlogfile
         logsquirl_regex
         Qt6::Core
)
