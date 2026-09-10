add_library(
  logsquirl_filewatch STATIC ${CMAKE_CURRENT_SOURCE_DIR}/include/filewatcher.h
                         ${CMAKE_CURRENT_SOURCE_DIR}/src/filewatcher.cpp
)

set_target_properties(logsquirl_filewatch PROPERTIES AUTOMOC ON)

target_include_directories(logsquirl_filewatch PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
# Deliberately not logsquirl_settings: file watching is handed a Watch
# Policy and reads no setting of its own, so an ambient read from here is
# a link error rather than something a reviewer has to catch (#93).
target_link_libraries(
  logsquirl_filewatch
  PUBLIC logsquirl_policies
         logsquirl_utils
         kdtoolbox
         efsw
)

if(LOGSQUIRL_USE_LTO)
  set_property(TARGET logsquirl_filewatch PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()
