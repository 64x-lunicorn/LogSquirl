# How a text view scrolls: the Scroll Position, follow with its elastic hook,
# the bottom of the Log File and re-wrapping, with the Viewport layout they
# are placed by (#246).
#
# It knows no widget: the text view adapts Qt events to it and applies its
# answers. tests/textviewscrolling_no_qt_widgets.cmake guards that no file of
# it includes a Qt Widgets header and that it links neither Qt Widgets, the
# settings store nor the UI library.
add_library(
  logsquirl_textviewscrolling STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include/textviewscrolling.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/viewportlayout.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/viewtools.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/wrappedstring.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/textviewscrolling.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/viewportlayout.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/viewtools.cpp
)

set_target_properties(logsquirl_textviewscrolling PROPERTIES AUTOMOC ON)

target_include_directories(logsquirl_textviewscrolling PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

# linetypes.h, and untabify() with it, lives in the log data library.
target_link_libraries(
  logsquirl_textviewscrolling
  PUBLIC project_options
         project_warnings
         logsquirl_policies
         logsquirl_logdata
         logsquirl_logging
         Qt6::Core
)

if(LOGSQUIRL_USE_LTO)
  set_property(TARGET logsquirl_textviewscrolling PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()
