add_library(
  logsquirl_highlighting STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include/highlightedmatch.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/highlighter.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/linedecorator.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/quickfindpattern.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/highlighter.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/linedecorator.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/quickfindpattern.cpp
)

set_target_properties(logsquirl_highlighting PROPERTIES AUTOMOC ON)

if(LOGSQUIRL_USE_LTO)
  set_property(TARGET logsquirl_highlighting PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

target_include_directories(logsquirl_highlighting PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(
  logsquirl_highlighting
  PUBLIC project_options
         project_warnings
         logsquirl_logging
         logsquirl_utils
         logsquirl_logdata
         logsquirl_regex
         Qt6::Core
         Qt6::Gui
         simdutf
)
