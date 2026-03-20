add_library(
  logsquirl_filewatch STATIC ${CMAKE_CURRENT_SOURCE_DIR}/include/filewatcher.h
                         ${CMAKE_CURRENT_SOURCE_DIR}/src/filewatcher.cpp
)

set_target_properties(logsquirl_filewatch PROPERTIES AUTOMOC ON)

target_include_directories(logsquirl_filewatch PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(
  logsquirl_filewatch
  PUBLIC logsquirl_settings
         logsquirl_utils
         kdtoolbox
         efsw
)

if(LOGSQUIRL_USE_LTO)
  set_property(TARGET logsquirl_filewatch PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()
