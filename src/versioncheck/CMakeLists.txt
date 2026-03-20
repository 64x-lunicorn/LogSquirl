add_library(
  logsquirl_versioncheck STATIC ${CMAKE_CURRENT_SOURCE_DIR}/include/versionchecker.h
                            ${CMAKE_CURRENT_SOURCE_DIR}/src/versionchecker.cpp
)

set_target_properties(logsquirl_versioncheck PROPERTIES AUTOMOC ON)

target_include_directories(logsquirl_versioncheck PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(
  logsquirl_versioncheck
  PUBLIC project_options
         project_warnings
         logsquirl_settings
         logsquirl_utils
         Qt${QT_VERSION_MAJOR}::Network
)
