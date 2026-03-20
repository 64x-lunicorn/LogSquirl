add_library(
  logsquirl_version STATIC ${CMAKE_CURRENT_SOURCE_DIR}/include/logsquirl_version.h
                       ${CMAKE_CURRENT_SOURCE_DIR}/src/logsquirl_version.cpp
)

target_include_directories(logsquirl_version PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(logsquirl_version PUBLIC project_options project_warnings Qt${QT_VERSION_MAJOR}::Core)

add_dependencies(logsquirl_version generate_version)
