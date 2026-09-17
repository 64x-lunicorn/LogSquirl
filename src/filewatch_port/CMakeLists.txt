# The File Watch Port: what an Open Log File hears about changes on disk
# through (#249). The log data and the Open Log File link only this, never the
# efsw watcher in src/filewatch: a lookup of the watcher from the engine is a
# build error.
add_library(
  logsquirl_filewatch_port STATIC ${CMAKE_CURRENT_SOURCE_DIR}/include/filewatchport.h
                                  ${CMAKE_CURRENT_SOURCE_DIR}/include/policyfilewatchport.h
                                  ${CMAKE_CURRENT_SOURCE_DIR}/src/filewatchport.cpp
)

set_target_properties(logsquirl_filewatch_port PROPERTIES AUTOMOC ON)

target_include_directories(logsquirl_filewatch_port PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
# The Watch Policy is a Settings Policy: the port names it, the settings store
# it does not.
target_link_libraries(logsquirl_filewatch_port PUBLIC project_options project_warnings Qt6::Core
                                                      logsquirl_policies)
