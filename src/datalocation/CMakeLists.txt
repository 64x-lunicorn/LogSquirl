# Where LogSquirl keeps its settings and data, portable or installed (#602).
# Every library that stores something asks it, so it needs only Qt Core and
# sits below all of them.
add_library(
  logsquirl_datalocation STATIC ${CMAKE_CURRENT_SOURCE_DIR}/include/datalocation.h
                                ${CMAKE_CURRENT_SOURCE_DIR}/src/datalocation.cpp
)

target_include_directories(logsquirl_datalocation PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(logsquirl_datalocation PUBLIC project_options project_warnings Qt6::Core)
target_link_libraries(logsquirl_datalocation PRIVATE whereami)
