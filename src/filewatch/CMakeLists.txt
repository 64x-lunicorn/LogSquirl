# The port an Open Log File hears about changes on disk through (#249). The
# log data and the Open Log File link only this, never the watcher below: a
# lookup of the watcher from the engine is a build error.
add_library(
  logsquirl_filewatch_port STATIC ${CMAKE_CURRENT_SOURCE_DIR}/port/include/filewatchport.h
                              ${CMAKE_CURRENT_SOURCE_DIR}/port/include/policyfilewatchport.h
                              ${CMAKE_CURRENT_SOURCE_DIR}/port/src/filewatchport.cpp
)

set_target_properties(logsquirl_filewatch_port PROPERTIES AUTOMOC ON)

target_include_directories(logsquirl_filewatch_port PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/port/include")
# The Watch Policy is a Settings Policy: the port names it, the settings store
# it does not.
target_link_libraries(logsquirl_filewatch_port PUBLIC project_options project_warnings Qt6::Core
                                                      logsquirl_policies)

# The efsw-based watcher: the adapter the application hands over.
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
  PUBLIC logsquirl_filewatch_port
         logsquirl_policies
         logsquirl_utils
         kdtoolbox
         efsw
)

if(LOGSQUIRL_USE_LTO)
  set_property(TARGET logsquirl_filewatch_port PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
  set_property(TARGET logsquirl_filewatch PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()
